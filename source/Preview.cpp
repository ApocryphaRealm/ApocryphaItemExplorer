#include "PCH.h"

#include "Preview.h"

#include "SKSEMenuFramework.h"

#include "PreviewMenu.h"
#include "Settings.h"

#include "utils/Logger.h"
#include "utils/Strings.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <string>

namespace preview
{
	namespace
	{
		// THE SPLIT THAT MATTERS. The page's render function runs on the RENDER thread, inside the
		// menu framework's Present hook. Everything below the line marked "main thread only" is
		// game work - Begin3D, LoadInventoryItem, attaching to the UI scene - and it does not
		// happen there. Measured 2026-09-09: called from the render thread, LoadInventoryItem
		// returns without complaint and the model is never built (its NewInventoryMenuItemLoadTask
		// never runs), so loadedModels stays empty and there is nothing to attach or draw. No
		// warning, no crash, no picture. So the render thread only ever records a WISH, and an
		// SKSE task carries it out on the main thread.
		std::mutex   g_lock;                  // guards the wish below
		RE::TESForm* g_requested = nullptr;   // what the page wants shown
		float        g_wishX = 0.0F, g_wishY = 0.0F, g_wishZ = 0.0F, g_wishScale = 0.0F;
		RE::TESForm* g_loaded = nullptr;      // what the game currently holds a model for
		bool         g_begun = false;         // Begin3D has been called and End3D has not
		std::uint32_t g_loads = 0;
		std::uint32_t g_failures = 0;
		bool         g_loggedUnavailable = false;
		bool         g_loggedNoScene = false;
		bool         g_sawManagerModel = false;

		// What is currently attached to the game's UI 3D scene, and how many times we have
		// attached - the counter is what a driving tool asserts on, because "a model is loaded"
		// and "a model is in the scene being rendered" are different claims.
		//
		// THE MOD OWNS THIS NODE. It is loaded here with BSModelDB::Demand rather than borrowed
		// from Inventory3DManager: that manager builds its model on a task the game only pumps
		// while an inventory-family menu is open, so asking it from a settings page left
		// loadedModels empty every time, with no warning and no picture (measured 2026-09-09,
		// four runs). Loading the NIF ourselves has no such precondition.
		RE::NiPointer<RE::NiNode> g_model;

		// The entry handed to the 3D manager. It has to outlive the call - the manager keeps a
		// reference to what it was given - so it is owned here rather than made on the stack.
		std::unique_ptr<RE::InventoryEntryData> g_entry;
		RE::NiAVObject*           g_attached = nullptr;
		std::uint32_t             g_attaches = 0;
		std::string               g_modelPath;
		std::string               g_lastError;

		// The RAW override, used to calibrate the mapping: a driving tool sweeps these and the
		// numbers that land the model inside the pane become the INI's mapping constants. Zero
		// scale means "no override" - the placement is derived from the pane instead.
		float g_rawX = 0.0F, g_rawY = 0.0F, g_rawZ = 0.0F, g_rawScale = 0.0F;

		// The last display size the framework reported. Kept so the settings page and the DevBench
		// tool can answer "where is the pane, in pixels?" on a frame of their own.
		float g_displayW = 0.0F, g_displayH = 0.0F;
		bool  g_frameDrawn = false;

		// What Tick() last handed the 3D manager, whichever path produced it - reported by the
		// DevBench tool so a calibration sweep can read back what it actually set.
		float g_appliedX = 0.0F, g_appliedY = 0.0F, g_appliedZ = 0.0F, g_appliedScale = 0.0F;

		// ImGui packs a colour as A<<24 | B<<16 | G<<8 | R.
		constexpr ImGuiMCP::ImU32 Col(int a_r, int a_g, int a_b, int a_a)
		{
			return static_cast<ImGuiMCP::ImU32>((a_a << 24) | (a_b << 16) | (a_g << 8) | a_r);
		}

		RE::Inventory3DManager* Manager()
		{
			auto* mgr = RE::Inventory3DManager::GetSingleton();

			// Rule 30: asked for, not assumed - and rule 17, a miss now is not a miss forever, so
			// this is not cached and not made fatal. It is logged once so a page that shows nothing
			// can be explained without guessing.
			if (!mgr && !g_loggedUnavailable)
			{
				g_loggedUnavailable = true;
				logger::warn("preview: the game's inventory 3D manager is not available; the item "
							 "preview will stay empty");
			}
			return mgr;
		}

		void Apply();  // main thread only - defined below Show/Hide

		// The NIF a form draws itself with. Weapons, armour pieces, books, ingredients and the rest
		// all carry a TESModel; anything that does not (a spell, say) has no model to show and is
		// declined rather than guessed at.
		std::string ModelPathFor(RE::TESForm* a_form)
		{
			if (!a_form) { return {}; }
			auto* model = a_form->As<RE::TESModel>();
			if (!model) { return {}; }
			const char* path = model->GetModel();
			if (!path || !path[0]) { return {}; }

			// The record stores the path relative to Meshes; the loader wants it from Data.
			std::string full(path);
			std::string lower = full;
			for (char& c : lower) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			if (lower.rfind("meshes\\", 0) != 0 && lower.rfind("meshes/", 0) != 0)
			{
				full = "meshes\\" + full;
			}
			return full;
		}

		void Stop(RE::Inventory3DManager* a_mgr)
		{
			// The scene detach comes FIRST and happens whether or not the 3D manager is reachable:
			// leaving our object parented into the game's menu scene after we stop showing it
			// would keep it on screen with nothing owning it.
			if (g_attached)
			{
				if (auto* scene = RE::UI3DSceneManager::GetSingleton()) { scene->DetachChild(g_attached); }
				g_attached = nullptr;
			}
			g_model.reset();
			g_entry.reset();
			g_modelPath.clear();
			SetPreviewMenuOpen(false);

			if (!a_mgr) { return; }

			if (g_loaded)
			{
				a_mgr->UnloadInventoryItem();
				g_loaded = nullptr;
			}
			if (g_begun)
			{
				a_mgr->End3D();
				g_begun = false;
			}
		}

		// The pane, in display pixels. Square, measured against the screen HEIGHT so it keeps its
		// shape on an ultrawide.
		bool PaneRect(float& a_x0, float& a_y0, float& a_x1, float& a_y1)
		{
			if (g_displayW <= 0.0F || g_displayH <= 0.0F) { return false; }
			const float side = settings::preview::paneSize * g_displayH;
			const float cx = settings::preview::paneX * g_displayW;
			const float cy = settings::preview::paneY * g_displayH;
			a_x0 = cx - side * 0.5F;
			a_y0 = cy - side * 0.5F;
			a_x1 = cx + side * 0.5F;
			a_y1 = cy + side * 0.5F;
			return true;
		}
	}

	void Show(RE::TESForm* a_form)
	{
		std::scoped_lock guard(g_lock);
		g_requested = a_form;
	}

	void Hide()
	{
		{
			std::scoped_lock guard(g_lock);
			g_requested = nullptr;
		}

		// The teardown is game work, so it goes to the main thread like everything else - but it is
		// queued right now rather than waiting for a Tick, because Hide() is what the page calls
		// when it stops being drawn and there may not BE another Tick.
		if (auto* tasks = SKSE::GetTaskInterface()) { tasks->AddTask([]() { Apply(); }); }
	}

	namespace
	{
		// MAIN THREAD ONLY. Everything here is game work: it is queued by Tick() through the
		// SKSE task interface and never called from the render thread.
		void Apply()
		{
			if (!settings::general::show3DPreview)
			{
				if (g_loaded || g_begun) { Stop(RE::Inventory3DManager::GetSingleton()); }
				return;
			}

			// The inventory 3D manager is no longer needed to LOAD anything - the mod owns that now -
			// so a null one is not a reason to stop. It is still asked for, because the placement
			// fields below are the ones the vanilla inventory reads and keeping them in step costs
			// nothing; Stop() handles a null manager on its own.
			auto* mgr = Manager();

			// One read of the wish, under the lock, so the whole of this pass works from a single
			// consistent snapshot rather than from values the render thread may change halfway.
			RE::TESForm* wanted = nullptr;
			float        wx = 0.0F, wy = 0.0F, wz = 0.0F, wscale = 0.0F;
			{
				std::scoped_lock guard(g_lock);
				wanted = g_requested;
				wx = g_wishX; wy = g_wishY; wz = g_wishZ; wscale = g_wishScale;
			}

			if (!wanted)
			{
				Stop(mgr);
				return;
			}

			if (wanted != g_loaded)
			{
				const std::string path = ModelPathFor(wanted);
				if (path.empty())
				{
					// Nothing to show, and that is a fact about the form rather than a failure -
					// said once per form, at debug, and the wish is dropped so it is not retried
					// every frame for the rest of the session.
					logger::debug("preview: {:08X} \"{}\" has no model to show",
								  wanted->GetFormID(), wanted->GetName());
					g_lastError = "no model on this form";
					Stop(mgr);
					std::scoped_lock guard(g_lock);
					g_requested = nullptr;
					return;
				}

				Stop(mgr);  // release whatever was showing before loading the next one

				RE::NiPointer<RE::NiNode>            node;
				RE::BSModelDB::DBTraits::ArgsType    args{};
				const auto err = RE::BSModelDB::Demand(path.c_str(), node, args);
				if (err != RE::BSResource::ErrorCode::kNone || !node)
				{
					++g_failures;
					g_lastError = std::format("Demand failed ({}) for {}", static_cast<int>(err), path);
					logger::warn("preview: {}", g_lastError);
					std::scoped_lock guard(g_lock);
					g_requested = nullptr;
					return;
				}

				// PUSH THE LIGHT SCHEME. This is what actually makes the game render the UI 3D
				// scene, and dropping it is why the model attached and stayed invisible. Measured
				// 2026-09-09 by reading UI3DSceneManager in three contexts: idle it sits at scheme
				// 4 with two on its stack; with the vanilla inventory open, and with Modex's menu
				// open, it reads scheme 1 (kInventory) with three. Both of those draw. Begin3D is
				// the call that pushes it - the manager is not needed to LOAD anything any more,
				// but it still owns the scheme stack.
				if (mgr && !g_begun)
				{
					mgr->Begin3D(static_cast<RE::INTERFACE_LIGHT_SCHEME>(settings::preview::scheme));
					g_begun = true;
					logger::info("preview: pushed the kInventory light scheme onto the UI 3D scene");
				}

				// The menu that makes the game render the scene at all. Opened only once there is
				// something to show, so nothing of ours is on the UI stack while the page is idle.
				SetPreviewMenuOpen(true);

				// AND ask the 3D manager for the same item, THE WAY THE GAME ASKS.
				//
				// The game's own UpdateItem3D handler was disassembled out of the running process
				// (its address read from the live InventoryMenu's fxDelegate): on a true argument
				// it fetches the selected entry and tail-calls Inventory3DManager::LoadInventoryItem
				// with an INVENTORY ENTRY - Address Library id 50884, the one-argument overload.
				//
				// Every earlier attempt here used the OTHER overload, id 50885, which takes a bound
				// object and an extra-data list. The game never calls that one, and loadedModels
				// stayed empty every single time. So this builds an entry, exactly as the item
				// menus do, and hands that over instead.
				//
				// Which overload is a setting (uLoadMode), because although both have been tried
				// and both left loadedModels empty, neither has been tried against the menu flag
				// recipes now under test - and SKSE's own item-display block calls the other one.
				if (auto* bound = wanted->As<RE::TESBoundObject>())
				{
					if (settings::preview::loadMode == 0)
					{
						g_entry = std::make_unique<RE::InventoryEntryData>(bound, 1);
						mgr->LoadInventoryItem(g_entry.get());
					}
					else
					{
						g_entry.reset();
						mgr->LoadInventoryItem(bound, nullptr);
					}
					logger::info("preview: load mode {} for {:08X}", settings::preview::loadMode,
								 bound->GetFormID());
				}

				g_model = node;
				g_modelPath = path;
				g_loaded = wanted;
				g_sawManagerModel = false;
				g_lastError.clear();
				++g_loads;

				logger::info("preview: loaded \"{}\" for {:08X} \"{}\"", path,
							 g_loaded->GetFormID(), g_loaded->GetName());
			}

			// THE MODEL GOES INTO THE GAME'S OWN UI 3D SCENE, and the game draws it inside its own
			// render pass. The first attempt asked Inventory3DManager to Render() from the menu
			// framework's Present hook instead - measured 2026-09-09: the manager holds the model
			// quite happily and nothing is ever composited, because by Present the pass that draw
			// would have joined has already finished. UI3DSceneManager is the system the inventory
			// itself renders through, so attaching to it puts the work where the engine expects it.
			if (auto* scene = RE::UI3DSceneManager::GetSingleton())
			{
				RE::NiAVObject* model = g_model.get();

				if (model != g_attached)
				{
					if (g_attached) { scene->DetachChild(g_attached); }
					g_attached = model;
					if (g_attached)
					{
						scene->AttachChild(g_attached,
									   static_cast<RE::INTERFACE_LIGHT_SCHEME>(settings::preview::scheme));

					// MAKE IT VISIBLE. A node handed back by BSModelDB::Demand has had none of the
					// setup the game's own load path does, and two things there will render it
					// perfectly invisibly: the hidden flag, and - for the BSFadeNode that weapon
					// and armour models are - a currentFade of 0, which is fully transparent. The
					// inventory never hits this because its load path fades its item in.
					g_attached->flags.reset(RE::NiAVObject::Flag::kHidden);
					if (auto* fade = netimmerse_cast<RE::BSFadeNode*>(g_attached))
					{
						fade->currentFade = 1.0F;
						logger::info("preview: BSFadeNode - fade forced to 1 (a Demand'd node "
									 "starts fully transparent)");
					}
						++g_attaches;
						logger::info("preview: attached \"{}\" to the UI 3D scene", g_loaded->GetName());
					}
				}

				// THE CAMERA IS LEFT ALONE BY DEFAULT, and that is a fix rather than an omission.
				// This used to call SetCameraPosition(0,0,0) and SetCameraFOV(45) every frame,
				// which drags the scene's camera to the origin with whatever rotation it happens
				// to hold - quite possibly pointing nowhere near the model. The game's own item
				// menus never move this camera: they position the ITEM and let the camera stand.
				//
				// The old behaviour stays reachable for experiments, but off unless asked for.
				if (settings::preview::overrideCamera)
				{
					scene->SetCameraFOV(settings::preview::camFov);
					scene->SetCameraPosition(RE::NiPoint3(settings::preview::camX,
														  settings::preview::camY,
														  settings::preview::camZ));
				}
			}
			else if (!g_loggedNoScene)
			{
				// Rule 17: not fatal, not cached - a miss now is not a miss forever - but said once so
				// an empty pane can be explained without guessing.
				g_loggedNoScene = true;
				logger::warn("preview: the UI 3D scene manager is not available; the model cannot be "
							 "drawn on this runtime");
			}

			// Placement, re-applied every frame because the manager owns these and the inventory
			// resets them when it runs.
			//
			// Two paths. The RAW override is the calibration path - a driving tool sets exact numbers
			// and reads back where the model landed. Otherwise the placement is DERIVED from the pane
			// the player positioned, through the linear mapping in the INI: the pane centre, in screen
			// fractions, becomes a horizontal and a vertical offset at a fixed depth. The constants
			// carry the signs, so an axis that runs the other way is an INI change, not a code change.
			// The placement was worked out on the render thread and handed over with the wish; it is
			// not recomputed here, so both threads can never disagree about where the model is.
			const float x = wx, y = wy, z = wz, scale = wscale;

			if (scale > 0.0F)
			{
				if (mgr)
				{
					mgr->itemPos = RE::NiPoint3(x, y, z);
					mgr->itemPosCopy = mgr->itemPos;
					mgr->itemScale = scale;
					mgr->itemScaleCopy = scale;
				}

				// Set on the OBJECT as well as on the manager. The manager applies its own copy during
				// the Render() we no longer rely on, so with the attach path the object's own local
				// transform is what actually decides where it sits and how big it is.
				if (g_attached)
				{
					g_attached->local.translate = RE::NiPoint3(x, y, z);
					g_attached->local.scale = scale;
					RE::NiUpdateData update{};
					g_attached->Update(update);
				}
			}
			g_appliedX = x; g_appliedY = y; g_appliedZ = z; g_appliedScale = scale;

			// The manager's async load finishing is worth exactly one log line - it is the moment
			// the half that actually draws becomes possible.
			if (!g_sawManagerModel && mgr && mgr->GetRuntimeData().loadedModels.size() > 0)
			{
				g_sawManagerModel = true;
				logger::info("preview: the 3D manager now holds {} model(s) - its async load finished",
							 mgr->GetRuntimeData().loadedModels.size());
			}

			// Inventory3DManager::Render(), on the MAIN THREAD. The first version called this from
			// the menu framework's Present hook and it drew nothing, which was written up as "this
			// call is the wrong mechanism". That verdict was drawn from the wrong thread: the
			// vanilla inventory calls it during its own menu render, on the main thread, and this
			// whole function now runs there too. Same call, the place the game makes it.
			if (mgr) { mgr->Render(); }
		}
	}

	void Tick()
	{
		// RENDER THREAD. This records what the page wants and asks the main thread to do it.
		// The work is queued every frame while a model is wanted but not yet attached, because
		// the game builds the model on a task of its own and it is not ready on the frame it
		// was asked for (rule 17 - a miss now is not a miss forever). Once it is attached the
		// queueing stops on its own.
		float x = 0.0F, y = 0.0F, z = 0.0F, scale = 0.0F;
		if (g_rawScale > 0.0F)
		{
			x = g_rawX; y = g_rawY; z = g_rawZ; scale = g_rawScale;
		}
		else
		{
			x = settings::preview::mapDepth;
			y = settings::preview::mapBaseY + (settings::preview::paneX - 0.5F) * settings::preview::mapSpanX;
			z = settings::preview::mapBaseZ + (0.5F - settings::preview::paneY) * settings::preview::mapSpanY;
			scale = settings::preview::mapScale * (settings::preview::paneSize / 0.28F);
		}

		bool wanted = false;
		{
			std::scoped_lock guard(g_lock);
			g_wishX = x; g_wishY = y; g_wishZ = z; g_wishScale = scale;
			wanted = g_requested != nullptr;
		}

		if (!settings::general::show3DPreview) { wanted = false; }
		if (!wanted && !g_attached && !g_begun) { return; }  // nothing wanted, nothing to undo

		// KEEP TASKING WHILE SOMETHING IS WANTED. This used to stop as soon as our own node was
		// attached, which quietly guaranteed the other half could never finish: the 3D manager
		// builds its model on an ASYNCHRONOUS task, so loadedModels is still empty on the frame
		// LoadInventoryItem is called and only fills in a frame or two later - by which time
		// nothing was running to notice. Modex's preview works through the manager's own models
		// (its scheme root has no children at all), so that half is the one that matters.

		if (auto* tasks = SKSE::GetTaskInterface()) { tasks->AddTask([]() { Apply(); }); }
	}

	void DrawFrame()
	{
		if (!settings::general::show3DPreview || !settings::preview::showFrame) { return; }

		ImGuiMCP::ImGuiIO* io = ImGuiMCP::GetIO();
		if (!io) { return; }
		g_displayW = io->DisplaySize.x;
		g_displayH = io->DisplaySize.y;

		float x0 = 0.0F, y0 = 0.0F, x1 = 0.0F, y1 = 0.0F;
		if (!PaneRect(x0, y0, x1, y1)) { return; }

		// The FOREGROUND list, so the frame sits over the framework window rather than under it.
		// The model itself cannot be lifted that way - it is drawn earlier in the frame, by the
		// game - so nothing is painted inside these lines. A fill here would hide what it frames.
		ImGuiMCP::ImDrawList* dl = ImGuiMCP::GetForegroundDrawList();
		if (!dl) { return; }

		constexpr ImGuiMCP::ImU32 kLine = Col(198, 168, 109, 210);  // the framework parchment gold
		constexpr ImGuiMCP::ImU32 kText = Col(232, 220, 196, 235);
		constexpr ImGuiMCP::ImU32 kDim = Col(232, 220, 196, 140);

		// Corner brackets rather than a closed box: they mark the area without drawing a line
		// across anything, and they read as a viewport instead of a window.
		const float len = (x1 - x0) * 0.18F;
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x0, y0 }, ImGuiMCP::ImVec2{ x0 + len, y0 }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x0, y0 }, ImGuiMCP::ImVec2{ x0, y0 + len }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x1, y0 }, ImGuiMCP::ImVec2{ x1 - len, y0 }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x1, y0 }, ImGuiMCP::ImVec2{ x1, y0 + len }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x0, y1 }, ImGuiMCP::ImVec2{ x0 + len, y1 }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x0, y1 }, ImGuiMCP::ImVec2{ x0, y1 - len }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x1, y1 }, ImGuiMCP::ImVec2{ x1 - len, y1 }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x1, y1 }, ImGuiMCP::ImVec2{ x1, y1 - len }, kLine, 2.0F);

		// The caption sits ABOVE the pane, never inside it.
		const char* caption = nullptr;
		ImGuiMCP::ImU32 captionCol = kText;
		if (!Available())
		{
			caption = strings::TR("AIE_PreviewNoRenderer", "3D preview unavailable on this runtime");
			captionCol = kDim;
		}
		else if (g_loaded)
		{
			caption = g_loaded->GetName();
			if (!caption || !caption[0]) { caption = strings::TR("AIE_PreviewUnnamed", "(no name)"); }
		}
		else
		{
			caption = strings::TR("AIE_PreviewPickARow", "Click a row to preview it here");
			captionCol = kDim;
		}
		ImGuiMCP::ImDrawListManager::AddText(dl, ImGuiMCP::ImVec2{ x0, y0 - 18.0F }, captionCol, caption, nullptr);

		g_frameDrawn = true;
	}

	RE::TESForm* Current() { return g_loaded; }

	void SetPlacement(float a_x, float a_y, float a_z, float a_scale)
	{
		g_rawX = a_x; g_rawY = a_y; g_rawZ = a_z; g_rawScale = a_scale;
		logger::info("preview: raw placement override {} to ({:.1f}, {:.1f}, {:.1f}) scale {:.3f}",
					 a_scale > 0.0F ? "set" : "cleared", a_x, a_y, a_z, a_scale);
	}

	void GetPlacement(float& a_x, float& a_y, float& a_z, float& a_scale)
	{
		a_x = g_rawX; a_y = g_rawY; a_z = g_rawZ; a_scale = g_rawScale;
	}

	void SetPane(float a_centreX, float a_centreY, float a_size)
	{
		// The settings ARE the state - nothing is cached here, so "Restore defaults" and a reload
		// of the INI both take effect on the next frame without a second copy to keep in step.
		// Clamped rather than trusted: a pane centred off-screen, or sized to nothing, is a preview
		// nobody can find and looks exactly like a broken renderer.
		settings::preview::paneX = std::clamp(a_centreX, 0.05F, 0.95F);
		settings::preview::paneY = std::clamp(a_centreY, 0.05F, 0.95F);
		settings::preview::paneSize = std::clamp(a_size, 0.08F, 0.90F);
		logger::debug("preview: pane set to ({:.3f}, {:.3f}) size {:.3f}", settings::preview::paneX,
					  settings::preview::paneY, settings::preview::paneSize);
	}

	bool GetPaneRect(float& a_x0, float& a_y0, float& a_x1, float& a_y1)
	{
		return PaneRect(a_x0, a_y0, a_x1, a_y1);
	}

	bool Available() { return RE::Inventory3DManager::GetSingleton() != nullptr; }

	SceneState GetSceneState()
	{
		SceneState st;
		auto* scene = RE::UI3DSceneManager::GetSingleton();
		if (!scene) { return st; }

		st.available = true;
		st.cameraPresent = scene->camera != nullptr;
		st.lightScheme = static_cast<std::uint32_t>(scene->currentlightScheme);
		st.lightCount = static_cast<std::uint32_t>(scene->menuLights.size());
		for (int i = 0; i < 8; ++i)
		{
			RE::NiNode* slot = scene->menuObjects[i].get();
			if (!slot) { continue; }
			++st.occupiedSlots;
			// menuObjects[] holds the eight scheme ROOTS, and our model is attached as a CHILD of
			// one of them - so comparing the model against a root can never match, and the -1 this
			// used to report was not evidence of anything. Ask whether the model's PARENT is this
			// root, which is the question that was meant all along (rule 30: ask the object).
			if (g_attached && g_attached->parent == slot) { st.ourSlot = i; }
		}

		// The same object seen through its other declaration, which is the one carrying the menu
		// bookkeeping: which menu the scene is currently rendering for, and how many are registered.
		auto* asRender = reinterpret_cast<RE::UIRenderManager*>(scene);
		st.currentMenu = asRender->currentMenu;
		st.menuIDCount = static_cast<std::uint32_t>(asRender->menuIDs.size());
		return st;
	}

	std::string OpenMenuFlags()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) { return "[]"; }

		std::string out = "[";
		bool        first = true;
		for (const auto& entry : ui->menuMap)
		{
			const auto& menu = entry.second.menu;
			if (!menu) { continue; }
			if (!first) { out += ','; }
			first = false;
			out += std::format(R"({{"name":"{}","offscreen":{},"flags":{}}})",
							   entry.first.c_str(),
							   menu->RendersOffscreenTargets() ? "true" : "false",
							   static_cast<std::uint32_t>(menu->menuFlags.underlying()));
		}
		out += ']';
		return out;
	}

	std::string ScanInventoryPreDisplay()
	{
		const auto base = REL::Module::get().base();

		// The vtable of the one menu that is known to render the scene.
		const REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_InventoryMenu[0] };
		const auto* slots = reinterpret_cast<const std::uintptr_t*>(vtable.address());
		if (!slots) { return R"({"error":"no vtable"})"; }

		// Every UI3DSceneManager method sits in one tight block on 1.5.97: AttachChild is at
		// +0x8D33B0 and SetCameraFOV at +0x8D39E0, with the class's other methods either side.
		// A call from this menu into that block is a call into the scene manager, whichever
		// method it turns out to be - so the search is for the block, not for a known function.
		constexpr std::uintptr_t kSceneLo = 0x8D1000;
		constexpr std::uintptr_t kSceneHi = 0x8D5000;

		std::string out = std::format(R"({{"base":"0x{:X}","vtable":"0x{:X}","hits":[)",
									  base, vtable.address() - base);
		bool first = true;

		// Every slot in the menu's vtable, not just PreDisplay - PreDisplay turned out to call
		// nothing in the block, and guessing a second slot would be another launch per guess.
		for (int slot = 0; slot < 32; ++slot)
		{
			const std::uintptr_t fn = slots[slot];
			if (fn < base || fn > base + 0x2000000) { continue; }

			const auto* code = reinterpret_cast<const std::uint8_t*>(fn);
			for (std::size_t i = 0; i + 5 < 0x600; ++i)
			{
				if (code[i] != 0xE8) { continue; }
				std::int32_t rel = 0;
				std::memcpy(&rel, code + i + 1, sizeof(rel));
				const std::uintptr_t target = fn + i + 5 + rel;
				if (target < base) { continue; }
				const std::uintptr_t off = target - base;
				if (off < kSceneLo || off >= kSceneHi) { continue; }
				if (!first) { out += ','; }
				first = false;
				out += std::format(R"({{"slot":{},"fn":"0x{:X}","at":{},"target":"0x{:X}"}})",
								   slot, fn - base, i, off);
			}
		}
		out += "]}";
		return out;
	}

	std::string FxCallbacks(const std::string& a_menuName)
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) { return R"({"error":"no UI"})"; }

		auto menu = ui->GetMenu(a_menuName);
		if (!menu) { return std::format(R"({{"error":"menu not open","menu":"{}"}})", a_menuName); }

		auto* fx = menu->fxDelegate.get();
		if (!fx) { return std::format(R"({{"error":"menu has no fxDelegate","menu":"{}"}})", a_menuName); }

		const auto base = REL::Module::get().base();
		std::string out = std::format(R"({{"menu":"{}","callbacks":[)", a_menuName);
		bool first = true;
		for (const auto& entry : fx->callbacks)
		{
			const char* name = entry.first.data();
			const auto* fn = reinterpret_cast<const std::uint8_t*>(entry.second.callback);
			if (!first) { out += ','; }
			first = false;
			out += std::format(R"({{"name":"{}","fn":"0x{:X}"}})",
							   name ? name : "?",
							   fn ? (reinterpret_cast<std::uintptr_t>(fn) - base) : 0);
		}
		out += "]}";
		return out;
	}

	std::string CallSitesOf(std::uintptr_t a_targetOffset)
	{
		const auto base = REL::Module::get().base();

		// Only the executable segment: scanning data for byte patterns would be noise, and .text is
		// where calls live anyway.
		const auto text = REL::Module::get().segment(REL::Segment::textx);
		const std::uintptr_t textStart = text.address();
		const std::uintptr_t textSize = text.size();
		if (a_targetOffset == 0 || textStart == 0 || textSize == 0) { return R"({"error":"no text segment"})"; }

		const std::uintptr_t target = base + a_targetOffset;
		const auto*          bytes = reinterpret_cast<const std::uint8_t*>(textStart);
		const std::uintptr_t size = textSize;

		std::string out = std::format(R"({{"target":"0x{:X}","textStart":"0x{:X}","textSize":"0x{:X}","sites":[)",
									  a_targetOffset, textStart - base, textSize);
		bool  first = true;
		int   found = 0;

		// E8 rel32: the direct call. An E8 byte can also fall inside another instruction's operand,
		// so a hit is a candidate rather than a certainty - but a candidate whose rel32 happens to
		// land exactly on this function is almost always real, and the ones that are not stand out
		// when the caller is disassembled.
		for (std::uintptr_t i = 0; i + 5 < size && found < 64; ++i)
		{
			if (bytes[i] != 0xE8) { continue; }
			std::int32_t rel = 0;
			std::memcpy(&rel, bytes + i + 1, sizeof(rel));
			if (textStart + i + 5 + rel != target) { continue; }
			if (!first) { out += ','; }
			first = false;
			++found;
			out += std::format(R"("0x{:X}")", textStart + i - base);
		}
		out += std::format(R"(],"found":{}}})", found);
		return out;
	}

	std::string DataRefsTo(std::uintptr_t a_targetOffset)
	{
		const auto base = REL::Module::get().base();
		if (a_targetOffset == 0) { return R"({"error":"no target"})"; }
		const std::uintptr_t target = base + a_targetOffset;

		// The read-only and read-write data segments: vtables live in .rdata, and function-pointer
		// tables the game builds at run time live in .data.
		const REL::Segment segments[] = {
			REL::Module::get().segment(REL::Segment::rdata),
			REL::Module::get().segment(REL::Segment::data)
		};
		static constexpr const char* names[] = { "rdata", "data" };

		std::string out = std::format(R"({{"target":"0x{:X}","refs":[)", a_targetOffset);
		bool first = true;
		int  found = 0;

		for (int seg = 0; seg < 2 && found < 32; ++seg)
		{
			const std::uintptr_t start = segments[seg].address();
			const std::uintptr_t size = segments[seg].size();
			if (!start || size < sizeof(std::uintptr_t)) { continue; }

			// Pointers are 8-byte aligned in these tables, so step by 8 rather than by 1.
			for (std::uintptr_t i = 0; i + sizeof(std::uintptr_t) <= size && found < 32; i += sizeof(std::uintptr_t))
			{
				std::uintptr_t value = 0;
				std::memcpy(&value, reinterpret_cast<const void*>(start + i), sizeof(value));
				if (value != target) { continue; }
				if (!first) { out += ','; }
				first = false;
				++found;
				out += std::format(R"({{"seg":"{}","at":"0x{:X}"}})", names[seg], start + i - base);
			}
		}
		out += std::format(R"(],"found":{}}})", found);
		return out;
	}

	std::string DumpBytes(std::uintptr_t a_offset, std::size_t a_length)
	{
		const auto base = REL::Module::get().base();
		const auto length = std::min<std::size_t>(a_length, 0x800);
		if (a_offset == 0 || a_offset > 0x2000000) { return R"({"error":"offset out of range"})"; }

		const auto* p = reinterpret_cast<const std::uint8_t*>(base + a_offset);
		std::string hex;
		hex.reserve(length * 2);
		for (std::size_t i = 0; i < length; ++i) { hex += std::format("{:02X}", p[i]); }
		return std::format(R"({{"offset":"0x{:X}","length":{},"bytes":"{}"}})", a_offset, length, hex);
	}

	void OpenGameMenu(const std::string& a_menuName)
	{
		if (auto* tasks = SKSE::GetTaskInterface())
		{
			const std::string name = a_menuName;
			tasks->AddTask([name]() {
				if (auto* q = RE::UIMessageQueue::GetSingleton())
				{
					q->AddMessage(RE::BSFixedString(name.c_str()), RE::UI_MESSAGE_TYPE::kShow, nullptr);
					logger::info("preview: asked the game to open \"{}\" (comparison run)", name);
				}
			});
		}
	}

	Status GetStatus()
	{
		Status s;
		s.available = Available();
		s.showing = g_loaded != nullptr;
		s.currentFormID = g_loaded ? g_loaded->GetFormID() : 0;
		s.loads = g_loads;
		s.failures = g_failures;
		PaneRect(s.paneX0, s.paneY0, s.paneX1, s.paneY1);
		s.posX = g_appliedX; s.posY = g_appliedY; s.posZ = g_appliedZ; s.scale = g_appliedScale;
		s.rawOverride = g_rawScale > 0.0F;
		s.sceneAvailable = RE::UI3DSceneManager::GetSingleton() != nullptr;
		s.hasParent = g_attached && g_attached->parent != nullptr;
		if (auto* ui = RE::UI::GetSingleton())
		{
			s.gamePaused = ui->GameIsPaused();
			s.pauseClaims = ui->numPausesGame;
		}
		if (auto* scene = RE::UI3DSceneManager::GetSingleton())
		{
			const auto idx = std::min<std::uint32_t>(settings::preview::scheme, 7);
			if (auto* root = scene->menuObjects[idx].get())
			{
				s.schemeRootChildren = static_cast<std::uint32_t>(root->children.size());
			}
		}
		s.attached = g_attached != nullptr;
		s.attaches = g_attaches;
		s.menuOpen = PreviewMenuOpen();
		if (auto* mgr = RE::Inventory3DManager::GetSingleton())
		{
			s.managerModels = static_cast<std::uint32_t>(mgr->GetRuntimeData().loadedModels.size());
			s.managerScheme = static_cast<std::uint32_t>(mgr->currentLightScheme);
		}
		s.modelPath = g_modelPath;
		s.lastError = g_lastError;
		s.frameDrawn = g_frameDrawn;
		return s;
	}
}

