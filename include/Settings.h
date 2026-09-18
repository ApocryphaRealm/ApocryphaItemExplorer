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

		// Shows the item under the cursor as a 3D model, in a pane beside the list. While the page
		// shows it the game is paused (the engine only draws inventory 3D for a paused menu).
		inline bool show3DPreview = true;        // bShow3DPreview:General

		// Items a quest calls its own. Shown by default but always marked, because handing yourself
		// a quest item can confuse the quest that owns it - and someone who does not want to see
		// them at all can switch them off here.
		inline bool showQuestItems = true;       // bShowQuestItems:General

		// Catalog::Sort - 0 A-Z, 1 Z-A, 2 value high, 3 value low, 4 weight high, 5 weight low.
		inline std::uint32_t sortMode = 0;       // uSortMode:General
	}

	// The 3D preview pane: how large it is on the page and how the model sits in it. The model is
	// drawn by the game at its own size; the pane shows a capture of it, so "scale" is how much of
	// that capture fills the pane, not a change to the model.
	namespace preview
	{
		// The box, in fractions of the screen: its centre, then its height (the box is square). It is
		// drawn in FRONT of the framework window, on the foreground list, so it can sit anywhere.
		inline float paneX = 0.80F;        // fPaneX:Preview
		inline float paneY = 0.50F;        // fPaneY:Preview
		inline float paneSize = 0.28F;     // fPaneSize:Preview
		// The corner brackets and the item-name caption above the box.
		inline bool  showFrame = true;     // bShowFrame:Preview
		inline float modelScale = 1.0F;    // fModelScale:Preview - 1 = as the game draws it; 0.5 = half
		inline float offsetX = 0.0F;       // fOffsetX:Preview - nudge the model in the pane, pixels
		inline float offsetY = 0.0F;       // fOffsetY:Preview
		// The flat colour behind the model (0-1): black by default, with a white frame and the model
		// drawn opaque in front (the owner, 2026-09-18: "a black background with a white frame around
		// it ... render the texture in front of the background with no fade or filter").
		inline float backgroundR = 0.0F;   // fBackgroundR:Preview
		inline float backgroundG = 0.0F;   // fBackgroundG:Preview
		inline float backgroundB = 0.0F;   // fBackgroundB:Preview
		inline float paneAlpha = 1.0F;     // fPaneAlpha:Preview - 1 = solid (the default); lower lets the page show through
	}

	void Init(const std::string& a_iniFileName);
	bool Reload();
	bool Save();
	void RestoreDefaults();
	void ApplyLogLevel();
	const std::string& GetIniPath();
}
