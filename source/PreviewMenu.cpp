#include "PCH.h"

#include "PreviewMenu.h"

#include "utils/Logger.h"

namespace preview
{
	namespace
	{
		bool g_registered = false;
	}

	PreviewMenu::PreviewMenu()
	{
		// The two flags the vanilla inventory has and the journal does not. kCustomRendering says
		// this menu is not drawn through a Scaleform movie, which is true - it draws nothing at
		// all - and kInventoryItemMenu is what marks it as one of the item-viewing family the UI
		// 3D scene is rendered for.
		menuFlags.set(RE::UI_MENU_FLAGS::kCustomRendering,
					  RE::UI_MENU_FLAGS::kInventoryItemMenu,
					  RE::UI_MENU_FLAGS::kRequiresUpdate,
					  // Not because the preview needs an offscreen target, but because PreDisplay
					  // is only called on menus that have this - and PreDisplay firing is the
					  // evidence that the game renders this menu at all.
					  RE::UI_MENU_FLAGS::kRendersOffscreenTargets);

		// Deliberately NOT kPausesGame and NOT kUsesCursor: this menu is invisible scaffolding, and
		// a settings page that silently paused the game or stole the cursor would be a bug the
		// player could see even though the menu itself is not.
		inputContext = Context::kNone;

		// The value IMenu itself defaults to, and what the inventory carries. It was 0 on the
		// first attempt, which put this menu below everything - and "rendered last" and "not
		// rendered" are indistinguishable from the outside.
		depthPriority = 3;
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
		// Once only: this is per-frame code, and the standing rule is that nothing there logs
		// unconditionally (rule 14).
		static bool logged = false;
		if (!logged)
		{
			logged = true;
			logger::info("preview menu: PreDisplay fired - the game IS rendering this menu");
		}

		// THE DRAW, in the one place it belongs. This is inside the game's own render pass, on a
		// menu the game is rendering - which is what every earlier attempt lacked. The same call
		// from the framework's Present hook drew nothing, and from a main-thread task drew
		// nothing, because neither is a render pass.
		if (auto* mgr = RE::Inventory3DManager::GetSingleton()) { mgr->Render(); }
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

	bool PreviewMenuOpen()
	{
		auto* ui = RE::UI::GetSingleton();
		return ui && ui->IsMenuOpen(PreviewMenu::MENU_NAME);
	}
}
