#pragma once

// ApocryphaRealm Item Explorer - the helper menu the 3D preview needs.
//
// The page is drawn by the menu framework, not by the game, and the game only builds and draws
// its inventory 3D for a MENU that is open with the inventory's kind of pause: kPausesGame
// together with kDisablePauseMenu (Modex issue #48 - with kPausesGame alone it silently does
// nothing, which is what 1.0.3 and 1.0.4 ran into). So this menu exists to be open, carry those
// flags, and hand its PostDisplay slot - the one the inventory renders its item from - to the
// preview's capture. It has no Scaleform movie (kCustomRendering) and draws nothing of its own.
//
// It is opened while the page keeps sending a heartbeat and closes itself when the page stops.

#include "RE/Skyrim.h"

namespace preview
{
	class PreviewMenu : public RE::IMenu
	{
	public:
		static constexpr std::string_view MENU_NAME = "AIEPreviewMenu";

		PreviewMenu();
		static RE::IMenu* Create();

		RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;
		void PostDisplay() override;
	};

	// Registered once, at kDataLoaded. Safe to call twice.
	void RegisterPreviewMenu();

	// Main-thread only (the UI message queue is the main thread's).
	void SetPreviewMenuOpen(bool a_open);

	[[nodiscard]] bool PreviewMenuOpen();
}
