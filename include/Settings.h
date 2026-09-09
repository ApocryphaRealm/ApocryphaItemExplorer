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

		// Shows the selected item's own model, through the game's inventory 3D renderer. On by
		// default because it is the fastest way to know what a form actually is when its name is
		// something like "DummyMarker01".
		inline bool show3DPreview = true;        // bShow3DPreview:General

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
	}

	void Init(const std::string& a_iniFileName);
	bool Reload();
	bool Save();
	void RestoreDefaults();
	void ApplyLogLevel();
	const std::string& GetIniPath();
}
