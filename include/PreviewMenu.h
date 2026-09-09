#pragma once

// ApocryphaRealm Item Explorer - the menu that exists so the preview can be drawn.
//
// WHY A MENU AT ALL, when the page it serves is drawn by the menu framework and not by the game:
// the game renders its UI 3D scene for a MENU, and only for a menu carrying the right flags.
// Measured over several runs on Test Build SE 1.5.97, 2026-09-09:
//
//   * with the model loaded, attached to UI3DSceneManager, and the kInventory light scheme pushed
//     - a scene state identical to the working case - nothing drew;
//   * with the JOURNAL open (game paused, menu mode, same scene state) nothing drew either, so
//     pausing is not it;
//   * with the vanilla INVENTORY open, it drew immediately.
//
// CommonLibSSE records what the inventory is, and it is the answer:
//     InventoryMenu flags = kPausesGame | kDisablePauseMenu | kUpdateUsesCursor |
//                           kInventoryItemMenu | kCustomRendering
// The journal has kPausesGame and neither of the last two. So this menu carries those two: it is
// registered with the game's UI, opened while a preview is showing and closed when it stops, and
// it exists purely to put the frame in the state where the scene gets rendered.
//
// It has NO Scaleform movie on purpose - kCustomRendering is the flag for a menu that does not
// draw itself through one - and it draws nothing. The page's pixels still come from the menu
// framework; this only makes the game willing to render the model behind them.

#include "RE/Skyrim.h"

namespace preview
{
	class PreviewMenu : public RE::IMenu
	{
	public:
		static constexpr std::string_view MENU_NAME = "AIEPreviewMenu";

		PreviewMenu();

		// The UI takes ownership of what this returns.
		static RE::IMenu* Create();

		RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;

		// Called by the game only for menus carrying kRendersOffscreenTargets. It is overridden
		// purely to find out whether the game renders this menu AT ALL: if it never fires, the
		// menu is being skipped rather than drawn with the wrong flags, and no combination of
		// flags will help - a menu with no Scaleform movie would need one first.
		void PreDisplay() override;
	};

	// Registered once, at kDataLoaded. Safe to call twice; the second is a no-op.
	void RegisterPreviewMenu();

	// Open and close it. Both are main-thread work and are queued, like the rest of the 3D path.
	void SetPreviewMenuOpen(bool a_open);

	// Whether the game currently reports it as open - read from the UI, not from a flag of ours,
	// because the whole point is what the GAME thinks is open (rule 30).
	[[nodiscard]] bool PreviewMenuOpen();
}
