#pragma once

// ApocryphaRealm Item Explorer - settings. Plain-file INI (redirector-proof, the project standard).

#include <cstdint>
#include <string>

namespace settings
{
	namespace debug
	{
		inline std::uint32_t logLevel = 0;  // uLogLevel:Debug
	}

	namespace general
	{
		inline std::uint32_t defaultCount = 1;   // uDefaultCount:General - how many an Add gives
		inline bool includeSpells = true;        // bIncludeSpells:General - list spells alongside items

		// Shows the selected item's own model, through the game's own UI 3D scene. OFF by default,
		// and deliberately: as of 1.0.3 the model loads, reaches the scene and the scene's render
		// is called, and nothing is painted (see the changelog). A switch that is on and does
		// nothing reads as a broken mod, so it ships off until it draws - one line to turn back on.
		inline bool show3DPreview = false;       // bShow3DPreview:General

		// Items a quest calls its own. Shown by default but always marked, because handing yourself
		// a quest item can confuse the quest that owns it - and someone who does not want to see
		// them at all can switch them off here.
		inline bool showQuestItems = true;       // bShowQuestItems:General

		// Catalog::Sort - 0 A-Z, 1 Z-A, 2 value high, 3 value low, 4 weight high, 5 weight low.
		inline std::uint32_t sortMode = 0;       // uSortMode:General
	}

	// Where the 3D preview appears, and how a place on the screen becomes a place in the game's
	// inventory renderer.
	//
	// The model is drawn by the game, earlier in the frame than the menu is composited, so the
	// menu window covers it wherever the two overlap. The pane is therefore positioned by the
	// player, and it defaults to the right-hand side of the screen, clear of a centred window.
	namespace preview
	{
		// The pane, in fractions of the screen: centre, then height (the pane is square).
		inline float paneX = 0.80F;        // fPaneX:Preview
		inline float paneY = 0.50F;        // fPaneY:Preview
		inline float paneSize = 0.28F;     // fPaneSize:Preview

		// The corner brackets and the caption that mark the pane. Off leaves the model unframed.
		inline bool showFrame = true;      // bShowFrame:Preview

		// The mapping from the pane to RE::Inventory3DManager::itemPos, which is in the renderer's
		// own units and not in pixels. It is linear: a fixed depth, plus a horizontal and a
		// vertical offset taken from how far the pane centre sits from the middle of the screen.
		// The spans carry the SIGN, so an axis that runs the other way on some runtime is an INI
		// change rather than a rebuild.
		inline float mapDepth = 25.0F;     // fMapDepth:Preview
		inline float mapBaseY = 0.0F;      // fMapBaseY:Preview
		inline float mapBaseZ = 0.0F;      // fMapBaseZ:Preview
		inline float mapSpanX = 40.0F;     // fMapSpanX:Preview
		inline float mapSpanY = 22.5F;     // fMapSpanY:Preview
		inline float mapScale = 1.0F;      // fMapScale:Preview - at the default pane size

		// The UI 3D scene's camera. The model is attached to the game's own menu scene
		// (UI3DSceneManager), which the game renders inside its own pass - the mod does not issue
		// the draw itself, because a draw issued from the menu overlay happens after that pass has
		// finished and produces nothing at all.
		// Off: the game's own item menus never move the UI scene's camera, and moving it to the
		// origin is a good way to point it away from the model.
		inline bool  overrideCamera = false;  // bOverrideCamera:Preview
		inline float camFov = 45.0F;       // fCamFov:Preview
		inline float camX = 0.0F;          // fCamX:Preview
		inline float camY = 0.0F;          // fCamY:Preview
		inline float camZ = 0.0F;          // fCamZ:Preview

		// Which INTERFACE_LIGHT_SCHEME the model is attached under and rendered with. They must
		// match: the render draws one scheme's root. 1 is kInventory - the inventory's own - but
		// the three menus that call the render directly use 7 (lockpicking), 6 (stats) and 4
		// (loading), so which schemes actually draw from a mod's own menu is a question to settle
		// by trying them rather than by assuming.
		inline std::uint32_t scheme = 1;   // uScheme:Preview
	}

	void Init(const std::string& a_iniFileName);
	bool Reload();
	bool Save();
	void RestoreDefaults();
	void ApplyLogLevel();
	const std::string& GetIniPath();
}
