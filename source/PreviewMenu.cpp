#include "PCH.h"

#include "PreviewMenu.h"

#include "Settings.h"

#include "utils/Logger.h"

namespace preview
{
	namespace
	{
		bool g_registered = false;

		// UI3DSceneManager::Render, Address Library ID 51855 on Skyrim SE 1.5.97.
		//
		// Hand-bound because CommonLibSSE does not carry it. The ID is version-specific and only
		// the 1.5.97 one has been established, so this refuses to run on any other runtime rather
		// than calling whatever happens to live at that address - a wrong address here is a crash,
		// not a compile error (rule 44).
		void RenderUIScene()
		{
			static const bool supported = [] {
				const auto version = REL::Module::get().version();
				const bool ok = version.major() == 1 && version.minor() == 5 && version.patch() == 97;
				if (!ok)
				{
					logger::warn("preview menu: the UI 3D scene render is only mapped for Skyrim "
								 "1.5.97; this runtime is {}, so the preview will not draw",
								 version.string());
				}
				return ok;
			}();
			if (!supported) { return; }

			auto* scene = RE::UI3DSceneManager::GetSingleton();
			if (!scene) { return; }

			using func_t = void (*)(RE::UI3DSceneManager*, RE::INTERFACE_LIGHT_SCHEME, RE::NiCamera*, bool);
			static REL::Relocation<func_t> render{ REL::ID(51855) };

			// A null camera makes the function use the scene's own, which is the one our page has
			// been positioning all along.
			render(scene, static_cast<RE::INTERFACE_LIGHT_SCHEME>(settings::preview::scheme),
				   nullptr, false);
		}
	}

	PreviewMenu::PreviewMenu()
	{
		// WHICH FLAGS, and why this is a runtime choice rather than a decision.
		//
		// Two configurations have a claim, and they disagree. Reproducing INVENTORYMENU's own set
		// is the obvious move and it is what 1.0.3 shipped - and it drew nothing. But SKSE's own
		// CustomMenu carries an item-display block (disabled in the shipped build) that configures
		// a CUSTOM menu quite differently, and UIExtensions ships the working version of exactly
		// that: magicmenuext is a mod-authored, non-vanilla menu whose ActionScript drives
		// UpdateItem3D, so a menu this project owns demonstrably CAN host the game's item 3D.
		//
		// Rather than pick one and spend a game launch finding out, the recipe is a setting and
		// the DevBench tool walks all of them in a single launch.
		switch (settings::preview::menuRecipe)
		{
		case 0:
			// InventoryMenu's own set, as CommonLibSSE records it. What 1.0.3 tried.
			menuFlags.set(RE::UI_MENU_FLAGS::kCustomRendering,
						  RE::UI_MENU_FLAGS::kInventoryItemMenu,
						  RE::UI_MENU_FLAGS::kRequiresUpdate,
						  RE::UI_MENU_FLAGS::kPausesGame);
			depthPriority = 3;
			break;

		case 2:
			// Recipe 1 with kRendersOffscreenTargets withheld, so that one flag is isolated. It is
			// the flag 1.0.3 deliberately removed, on the reasoning that the inventory does not
			// carry it - true, but SKSE's block sets it precisely because a CUSTOM menu is not the
			// inventory.
			menuFlags.set(RE::UI_MENU_FLAGS::kModal, RE::UI_MENU_FLAGS::kPausesGame);
			depthPriority = 0xA;
			break;

		case 3:
			// Both families at once, in case the two sets are complementary rather than rival.
			menuFlags.set(RE::UI_MENU_FLAGS::kModal,
						  RE::UI_MENU_FLAGS::kPausesGame,
						  RE::UI_MENU_FLAGS::kRendersOffscreenTargets,
						  RE::UI_MENU_FLAGS::kCustomRendering,
						  RE::UI_MENU_FLAGS::kInventoryItemMenu);
			depthPriority = 0xA;
			break;

		case 1:
		default:
			// SKSE's CustomMenu, as its own source configures it for item display:
			//     flags = kFlag_Modal | kFlag_PausesGame [| kFlag_RendersOffscreenTargets]
			//     unk0C = 0xA          <- depthPriority
			// The cursor flags it adds when no gamepad is present are NOT copied: taking the
			// cursor from the framework's own window would be visible to the player.
			menuFlags.set(RE::UI_MENU_FLAGS::kModal,
						  RE::UI_MENU_FLAGS::kPausesGame,
						  RE::UI_MENU_FLAGS::kRendersOffscreenTargets);
			depthPriority = 0xA;
			break;
		}

		// Never kUsesCursor, under any recipe, for the reason above.
		inputContext = Context::kNone;

		// THE MOVIE, and it is one of the three things under test.
		//
		// 1.0.3 handed this menu a 20-byte blank movie, on the reasoning that the menu draws
		// nothing of its own. That reasoning has a hole in it: with kRendersOffscreenTargets the
		// menu's rendering goes to an offscreen target, and a movie that composites nothing may be
		// exactly why nothing ever reached the screen. Worse, it made two different failures
		// indistinguishable - "the game never rendered this menu" and "the game rendered it and
		// the 3D was absent" both look like an unchanged frame.
		//
		// The marker movie draws one small rectangle in the top-left and nothing else. If it
		// appears, this menu is being composited and the remaining question is only the model. If
		// it does not, nothing about the 3D has been tested yet. Both movies are ours, generated
		// by .MD/scripts/make-marker-swf.py and make-blank-swf.py - no vanilla SWF is loaded by
		// path and no other mod's file is reused.
		const char* movie = settings::preview::markerMovie ? "ApocryphaItemExplorer/preview_marker"
														   : "ApocryphaItemExplorer/preview";
		if (auto* scaleform = RE::BSScaleformManager::GetSingleton())
		{
			const bool loaded = scaleform->LoadMovieEx(
				this, movie, RE::BSScaleformManager::ScaleModeType::kShowAll, 0.0F,
				[](RE::GFxMovieDef*) {});
			logger::info("preview menu: recipe {}, depth {}, movie \"{}\" {}",
						 settings::preview::menuRecipe, depthPriority, movie,
						 loaded ? "loaded" : "FAILED to load");
		}
		else
		{
			// Rule 14: a lookup that can return null is checked, and the miss is logged at a level
			// that means something went wrong - a menu with no movie is not a working menu.
			logger::error("preview menu: BSScaleformManager is unavailable; no movie was loaded");
		}
	}

	RE::IMenu* PreviewMenu::Create()
	{
		return new PreviewMenu();
	}

	RE::UI_MESSAGE_RESULTS PreviewMenu::ProcessMessage(RE::UIMessage&)
	{
		// Every message is accepted and ignored. There is no movie to advance and no input to take.
		return RE::UI_MESSAGE_RESULTS::kHandled;
	}

	void PreviewMenu::PreDisplay()
	{
		// Kept only as evidence that the game renders this menu at all. The DRAW is in PostDisplay,
		// which is where the game's own menus do it.
		static bool logged = false;
		if (!logged)
		{
			logged = true;
			logger::info("preview menu: PreDisplay fired - the game IS rendering this menu");
		}
	}

	void PreviewMenu::PostDisplay()
	{
		// EXACTLY WHAT THE GAME'S INVENTORY DOES, IN THE ORDER IT DOES IT.
		//
		// InventoryMenu::PostDisplay is nine instructions. Its address came out of the live vtable
		// (slot 6, +0x88DAE0 on 1.5.97) and it disassembles to:
		//
		//     mov  rcx, [this+0x10]              ; uiMovie
		//     test rcx, rcx / je                 ; only if it has one
		//     call [rax+0x130]                   ; DISPLAY THE MOVIE FIRST
		//     mov  rcx, [Inventory3DManager]
		//     jmp  Inventory3DManager::Render    ; THEN the 3D
		//
		// Two things every earlier attempt had wrong. The ORDER: the 3D is rendered after the movie
		// is displayed, not before. And the CALL: a bare Render(), no light scheme and no camera -
		// so the scheme sweep and the hand-bound UI3DSceneManager render were both answering a
		// question the game never asks.
		IMenu::PostDisplay();

		if (auto* mgr = RE::Inventory3DManager::GetSingleton()) { mgr->Render(); }

		// Once only - this is per-frame code (rule 14).
		static bool logged = false;
		if (!logged)
		{
			logged = true;
			logger::info("preview menu: PostDisplay - movie displayed, then Inventory3DManager::Render");
		}
	}

	void RegisterPreviewMenu()
	{
		if (g_registered) { return; }

		auto* ui = RE::UI::GetSingleton();
		if (!ui)
		{
			// Rule 17: not ready is not never - this is called again at the next message.
			logger::debug("preview menu: the UI is not available yet; will register later");
			return;
		}

		ui->Register(PreviewMenu::MENU_NAME, PreviewMenu::Create);
		g_registered = true;
		logger::info("preview menu: registered \"{}\" (kCustomRendering | kInventoryItemMenu)",
					 std::string(PreviewMenu::MENU_NAME));
	}

	void SetPreviewMenuOpen(bool a_open)
	{
		if (!g_registered) { return; }
		if (PreviewMenuOpen() == a_open) { return; }

		auto* queue = RE::UIMessageQueue::GetSingleton();
		if (!queue) { return; }

		queue->AddMessage(RE::BSFixedString(std::string(PreviewMenu::MENU_NAME).c_str()),
						  a_open ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide,
						  nullptr);
		logger::info("preview menu: asked to {} it", a_open ? "open" : "close");
	}

	void RebuildMenu()
	{
		// The flags and the movie are both chosen in the constructor, so changing the recipe means
		// nothing until a NEW menu object exists. The game builds one per open through the creator
		// registered above, so closing it is enough: Tick reopens it on the next frame it has
		// something to show, and that reopen runs the constructor again under the new recipe.
		//
		// Queued onto the main thread because this is called from DevBench's listener thread, and
		// the UI message queue is not ours to touch from there (rule 64).
		if (auto* tasks = SKSE::GetTaskInterface())
		{
			tasks->AddTask([]() {
				if (PreviewMenuOpen()) { SetPreviewMenuOpen(false); }
				logger::info("preview menu: closed for a rebuild under recipe {}",
							 settings::preview::menuRecipe);
			});
		}
	}

	bool PreviewMenuOpen()
	{
		auto* ui = RE::UI::GetSingleton();
		return ui && ui->IsMenuOpen(PreviewMenu::MENU_NAME);
	}
}
