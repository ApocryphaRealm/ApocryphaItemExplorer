#include "PCH.h"

#include "PreviewMenu.h"

#include "Preview.h"

#include "utils/Logger.h"

namespace preview
{
	namespace
	{
		bool g_registered = false;
	}

	PreviewMenu::PreviewMenu()
	{
		// The inventory's kind of pause, and nothing that would take the cursor or the input
		// context away from the framework's window (Modex's own menu adds cursor flags because it
		// IS the window; this one sits behind someone else's).
		menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame,
					  RE::UI_MENU_FLAGS::kDisablePauseMenu,
					  RE::UI_MENU_FLAGS::kCustomRendering);
		depthPriority = 11;
		inputContext = Context::kNone;
	}

	RE::IMenu* PreviewMenu::Create() { return new PreviewMenu(); }

	RE::UI_MESSAGE_RESULTS PreviewMenu::ProcessMessage(RE::UIMessage& a_message)
	{
		switch (a_message.type.get())
		{
		case RE::UI_MESSAGE_TYPE::kShow:
			OnMenuShown();
			break;
		case RE::UI_MESSAGE_TYPE::kHide:
			OnMenuHidden();
			break;
		default:
			break;
		}
		return IMenu::ProcessMessage(a_message);
	}

	void PreviewMenu::PostDisplay()
	{
		IMenu::PostDisplay();
		if (MenuShouldClose())
		{
			SetPreviewMenuOpen(false);
			return;
		}
		RenderFromMenu();
	}

	void RegisterPreviewMenu()
	{
		if (g_registered) { return; }
		auto* ui = RE::UI::GetSingleton();
		if (!ui) { logger::debug("preview menu: the UI is not available yet"); return; }
		ui->Register(PreviewMenu::MENU_NAME, PreviewMenu::Create);
		g_registered = true;
		logger::info("preview menu: registered \"{}\" (kPausesGame | kDisablePauseMenu | kCustomRendering)", std::string(PreviewMenu::MENU_NAME));
	}

	void SetPreviewMenuOpen(bool a_open)
	{
		if (!g_registered) { return; }
		if (PreviewMenuOpen() == a_open) { return; }
		auto* queue = RE::UIMessageQueue::GetSingleton();
		if (!queue) { return; }
		queue->AddMessage(RE::BSFixedString(std::string(PreviewMenu::MENU_NAME).c_str()),
						  a_open ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide, nullptr);
		logger::debug("preview menu: asked to {}", a_open ? "open" : "close");
	}

	bool PreviewMenuOpen()
	{
		auto* ui = RE::UI::GetSingleton();
		return ui && ui->IsMenuOpen(PreviewMenu::MENU_NAME);
	}
}
