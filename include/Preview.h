#pragma once

// ApocryphaRealm Item Explorer - the 3D item preview, take two (2026-09-18).
//
// The game already knows how to draw exactly one inventory item's model: RE::Inventory3DManager,
// the thing the inventory uses when you highlight something. What kept the first attempt dark
// (1.0.3-1.0.4, see CHANGELOG and logic library entries of 2026-09-09/10) was WHERE the model
// went and WHAT the host menu needed:
//
//   * the manager only builds and draws its model for a MENU that pauses the game, and the pause
//     has to be the inventory-style one - kPausesGame TOGETHER WITH kDisablePauseMenu. With only
//     kPausesGame set nothing is built and nothing says why (patchulidev / cyfewlp, Modex issue
//     #48, which is where the recipe below comes from; Modex ships it, so it is measured to work);
//   * the model is painted onto the BACK BUFFER during that menu's own render slot, before the
//     menu framework composites its window on top. So the page cannot simply leave a hole for it
//     - it has to be lifted off the back buffer into a texture the page can draw as an image.
//
// So this is how it works now. A tiny helper menu (PreviewMenu.h) carrying those flags is opened
// while the page wants a preview. In that menu's PostDisplay - the same slot the inventory
// renders its item in - the rectangle where the engine is about to paint the model is saved,
// cleared to a flat colour, the manager's Render() is called, the rectangle is copied into a
// texture this mod owns, and the saved pixels are put back so the player never sees the model
// twice. The page then draws that texture with ImGui::Image, inside its own layout, wherever the
// pane is. Both halves are the engine's own work; this mod draws no triangles.
//
// The capture pipeline (save / clear / render / capture / restore) is ported from Modex - Mod
// Explorer Menu by Patchuli (src/ui/components/Item3DPreview.cpp, MIT / GPL-3.0 dual-licensed);
// see THIRD_PARTY_NOTICES.md.

#include <cstdint>
#include <string>

namespace RE
{
	class TESBoundObject;
	class TESForm;
}

namespace preview
{
	// False when the game's 3D manager or the renderer cannot be reached - the page says so rather
	// than leaving an empty panel that looks like a broken model.
	[[nodiscard]] bool Available();

	// ---- Called from the PAGE (the menu framework's Present hook) --------------------------------

	// "The page is showing the pane this frame." Keeps the helper menu open; a page that stops
	// calling this (closed, another tab) lets the menu close itself a moment later, which unpauses
	// the game and releases the model.
	void Heartbeat();

	// Ask for a form to be shown, and say how large the pane is (display pixels). Cheap and
	// idempotent; the page calls it every frame with whatever row is under the cursor.
	void Request(RE::TESForm* a_form, float a_paneWidth, float a_paneHeight);

	// What to draw: the texture (an ImTextureID for the framework's igImage, null until a capture
	// has landed), and the UV rectangle that crops the capture to the model.
	[[nodiscard]] void* TextureID();
	[[nodiscard]] bool HasImage();
	void DisplayUV(float& a_u0, float& a_v0, float& a_u1, float& a_v1);

	// The floating box: drawn on the framework's FOREGROUND draw list, so it sits in front of the
	// framework window wherever the pane settings put it. The capture (or the flat background
	// while nothing has landed), corner brackets and the caption above. Call every frame the page
	// is drawn, after Heartbeat and Request. a_title is the item's name (may be null).
	void DrawFloating(const char* a_title);

	// The box in display pixels, from the settings. False before the framework has drawn a frame.
	[[nodiscard]] bool PaneRect(float& a_x0, float& a_y0, float& a_x1, float& a_y1);

	// ---- Called from the HELPER MENU (main thread, the game's own menu pass) --------------------

	void OnMenuShown();    // Begin3D(kInventory)
	void OnMenuHidden();   // unload, End3D, release the textures
	void RenderFromMenu(); // load the requested item if it changed, then save/clear/render/capture/restore

	// True when the page has not been seen for a while and the menu should close.
	[[nodiscard]] bool MenuShouldClose();

	// ---- For the DevBench tool ------------------------------------------------------------------
	struct Status
	{
		bool          available = false;
		bool          menuOpen = false;
		bool          running = false;       // Begin3D done
		bool          textures = false;      // capture textures created
		std::uint32_t currentFormID = 0;
		std::uint32_t requestedFormID = 0;
		std::uint32_t loads = 0;
		std::uint32_t renders = 0;
		std::uint32_t managerModels = 0;     // Inventory3DManager::loadedModels.size(), read off the engine
		float         capturedW = 0.0F, capturedH = 0.0F;
		float         paneW = 0.0F, paneH = 0.0F;
		double        sinceHeartbeatMs = 0.0;
		int           inventory3DSetting = -1;   // bShowInventory3D:Interface as the engine holds it (-1 = no such setting)
		bool          settingForced = false;     // this mod turned it on for the session
		// The last capture's geometry (diagnostic): the rectangle on the back buffer, the model's
		// bound centre / radius / translation, the projected screen point, the screen size.
		int           rectL = 0, rectT = 0, rectW = 0, rectH = 0;
		float         boundX = 0, boundY = 0, boundZ = 0, radius = 0, transX = 0, transY = 0, transZ = 0;
		float         screenX = 0, screenY = 0;
		std::uint32_t screenW = 0, screenH = 0;
		bool          noRestore = false;
		std::uint32_t targetW = 0, targetH = 0;   // the bound render target at PostDisplay
		int           targetFormat = -1;
		std::string   lastError;
	};
	[[nodiscard]] Status GetStatus();

	// Diagnostic: leave the engine's paint on the back buffer (skip the restore) so a frame shows it.
	void SetNoRestore(bool a_on);
}
