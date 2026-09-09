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

		// Shows the selected item's own model, through the game's own UI 3D scene.
		//
		// ON in this build, and it must go back OFF before anything is finalized unless a capture
		// shows a model. 1.0.3 shipped it off for the right reason - a switch that is on and does
		// nothing reads as a broken mod - and 1.0.4 exists only to test the configurations below,
		// which cannot be tested with the feature switched off.
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

		// --- The configuration sweep (diagnostic; see CHANGELOG 1.0.4) -------------------------
		//
		// A mod-owned menu CAN host the game's item 3D - UIExtensions' magicmenuext.swf is a
		// shipping, non-vanilla menu that does exactly that. What is not known is which part of
		// how THIS menu is set up differs from the one that works, and every earlier attempt spent
		// a whole game launch answering one guess. These three make the configuration a runtime
		// value, so the DevBench tool can walk every combination inside ONE launch (rule 64).
		//
		// uMenuRecipe - the flag set the menu is constructed with:
		//   0  InventoryMenu's own, which is what was tried and failed:
		//      kCustomRendering | kInventoryItemMenu | kRequiresUpdate | kPausesGame, depth 3
		//   1  SKSE's own CustomMenu item-display block - the recipe UIExtensions ships:
		//      kModal | kPausesGame | kRendersOffscreenTargets, depth 0xA
		//   2  recipe 1 without kRendersOffscreenTargets - isolates that one flag
		//   3  recipe 1 plus kCustomRendering | kInventoryItemMenu - both families at once
		inline std::uint32_t menuRecipe = 1;   // uMenuRecipe:Preview

		// There is deliberately NO "render site" knob. SKSE's CustomMenu calls the 3D manager's
		// Render from IMenu virtual slot 06, which it names Render and CommonLibSSE-NG names
		// PostDisplay - the same slot under two reverse-engineered names. PostDisplay below is
		// already that site, so there is no second one to try.

		// uLoadMode - which overload builds the model. BOTH have already been tried and both left
		// loadedModels empty, so this is the weakest of the three knobs; it is kept only because
		// the two have never been tried against the flag recipes below.
		//   0  LoadInventoryItem(InventoryEntryData*), Address Library 50884. THE DEFAULT, because
		//      the game's own UpdateItem3D handler was disassembled out of the running process and
		//      tail-calls this one.
		//   1  LoadInventoryItem(TESBoundObject*, nullptr), id 50885 - the overload SKSE's
		//      CustomMenu block calls (as UpdateMagic3D, its own name for the same address).
		inline std::uint32_t loadMode = 0;     // uLoadMode:Preview

		// bMarkerMovie - hand the menu a movie that DRAWS something (a marker rectangle) rather
		// than the 20-byte blank one. This is the observable that separates "the game never
		// composited this menu" from "it composited it and the 3D was absent" - two failures that
		// have been indistinguishable in every run so far.
		inline bool markerMovie = true;        // bMarkerMovie:Preview
	}

	void Init(const std::string& a_iniFileName);
	bool Reload();
	bool Save();
	void RestoreDefaults();
	void ApplyLogLevel();
	const std::string& GetIniPath();
}
