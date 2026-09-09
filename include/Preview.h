#pragma once

// ApocryphaRealm Item Explorer - the 3D preview.
//
// The game already knows how to render exactly one inventory item's model at a time: it is what
// the inventory does when you highlight something. RE::Inventory3DManager owns that, and it takes
// a base object - which is precisely what a catalogue of forms has. So this draws nothing itself
// and copies nothing; it asks the game to show the selected item, and asks it to stop.
//
// One item at a time is not a limitation worked around, it is the shape of the underlying system.
//
// WHERE it appears is the hard half. The model goes through the game's renderer; the framework's
// menu is composited afterwards, in its Present hook. So anything this mod's page paints over the
// model HIDES it - which is why the pane below is a border and a caption with nothing painted
// inside it, and why the pane is meant to be placed clear of the framework's own window.

#include <cstdint>
#include <string>

namespace RE
{
	class TESBoundObject;
	class TESForm;
}

namespace preview
{
	// Ask for a form to be shown. Cheap and idempotent - passing the same form twice does nothing,
	// which matters because the page calls this every frame from its selection.
	void Show(RE::TESForm* a_form);

	// Stop showing anything and release the model. Safe to call when nothing is shown.
	void Hide();

	// Called once per frame while the page is visible. Loads a newly requested model and renders
	// the current one. Doing the work here rather than in Show() keeps every call into the game's
	// 3D manager on the frame it belongs to.
	void Tick();

	// The pane's border and caption, drawn on the framework's FOREGROUND draw list so they sit
	// over the menu window rather than under it. Nothing is painted INSIDE the border: every pixel
	// this mod draws there would cover the model, which is drawn earlier in the frame.
	// Call it from the page's render function, every frame the page is drawn.
	void DrawFrame();

	// What is being shown, or nullptr.
	[[nodiscard]] RE::TESForm* Current();

	// Where the model sits and how big it is, in the 3D manager's own units. This is the RAW
	// override used to calibrate the mapping below - a driving tool sweeps it and the resulting
	// numbers become the mapping constants. A scale of zero means "no override; derive the
	// placement from the pane".
	void SetPlacement(float a_x, float a_y, float a_z, float a_scale);
	void GetPlacement(float& a_x, float& a_y, float& a_z, float& a_scale);

	// The pane, in fractions of the screen: its centre and its height (the pane is square).
	// This is the shipped path - the player positions a rectangle, and the model is placed to
	// land inside it through the mapping constants in the INI.
	void SetPane(float a_centreX, float a_centreY, float a_size);

	// The pane in display pixels, for the settings page and for a driving tool. Returns false
	// when the display size is not known yet (the framework has not drawn a frame).
	bool GetPaneRect(float& a_x0, float& a_y0, float& a_x1, float& a_y1);

	// False when the game's 3D manager could not be reached at all - the page says so rather than
	// leaving an empty panel that looks like a broken model.
	[[nodiscard]] bool Available();

	// For the DevBench status tool.
	struct Status
	{
		bool          available = false;
		bool          showing = false;
		std::uint32_t currentFormID = 0;
		std::uint32_t loads = 0;
		std::uint32_t failures = 0;
		float         paneX0 = 0.0F, paneY0 = 0.0F, paneX1 = 0.0F, paneY1 = 0.0F;
		float         posX = 0.0F, posY = 0.0F, posZ = 0.0F, scale = 0.0F;
		bool          rawOverride = false;
		bool          frameDrawn = false;

		// "A model is loaded" and "a model is in the scene the game renders" are different claims,
		// and the first one being true while the second was false is exactly what made the earlier
		// attempt look like it was working. They are reported separately for that reason.
		bool          sceneAvailable = false;
		bool          attached = false;
		std::uint32_t attaches = 0;

		// Read from the ENGINE, not from our own pointer. "attached" above is only this mod's flag
		// saying it called AttachChild; these two say whether the node is actually parented into
		// the scheme root the render draws. That distinction has already caused two wrong
		// conclusions in this mod, so it is measured rather than assumed.
		bool          hasParent = false;
		std::uint32_t schemeRootChildren = 0;

		// Inventory3DManager::currentLightScheme (+0x34). THIS is the value its Render passes to
		// the scene render - not UI3DSceneManager's own +0x90, which is what has been reported all
		// along. If the two disagree, the render draws a different scheme's root, which would be
		// empty.
		std::uint32_t managerScheme = 99;

		// Whether the game is ACTUALLY paused, and how many open menus claim kPausesGame. Setting
		// the flag is not the same as the game pausing, and the engine only renders the UI 3D
		// scene while it is paused - so this is the effect, read from the UI, not our intent.
		bool          gamePaused = false;
		std::uint32_t pauseClaims = 0;

		// What the mod loaded, and why it did not - so a run says which of the two halves failed
		// without a second launch to find out.
		bool          menuOpen = false;
		std::uint32_t managerModels = 0;   // Inventory3DManager::loadedModels.size()
		std::string   modelPath;
		std::string   lastError;
	};

	[[nodiscard]] Status GetStatus();

	// What the game's UI 3D scene currently holds, read straight off the object rather than
	// inferred (rule 30). The comparison this exists for: read it while OUR page is showing an
	// item, then again while the game's own inventory is showing one, and the difference is the
	// precondition we are missing.
	struct SceneState
	{
		bool          available = false;
		bool          cameraPresent = false;
		int           occupiedSlots = 0;
		int           ourSlot = -1;
		std::uint32_t lightScheme = 0;
		std::uint32_t currentMenu = 0;
		std::uint32_t menuIDCount = 0;
		std::uint32_t lightCount = 0;
	};

	[[nodiscard]] SceneState GetSceneState();

	// Every menu the game currently has open, with the flag that matters: kRendersOffscreenTargets
	// is what gets a menu the offscreen 3D pass, and a menu carrying it is what made our attached
	// model appear. This answers whether anything we can already open carries it, before writing a
	// menu of our own to carry it.
	[[nodiscard]] std::string OpenMenuFlags();

	// Read the game's own InventoryMenu::PreDisplay and report every function it calls, as offsets
	// from the module base. That menu is the only context in which our model has ever been drawn,
	// so whatever renders the UI 3D scene is among these - and the offsets map back to Address
	// Library IDs, which is how the answer becomes something we can call.
	//
	// Read-only, and it reads the RUNNING process on purpose: the on-disk SkyrimSE.exe is
	// Steam-packed, so the same bytes cannot be found by disassembling the file (PREFLIGHT).
	[[nodiscard]] std::string ScanInventoryPreDisplay();

	// Every GameDelegate callback a live menu has registered, with the address of each handler.
	// This is how the inventory's own "UpdateItem3D" is located: the game's ActionScript calls it
	// to show an item, so whatever it does is what a mod has to do too.
	[[nodiscard]] std::string FxCallbacks(const std::string& a_menuName);

	// Every direct call site of a function, found by scanning the whole module. This is how you
	// ask "who calls this?" of a stripped binary: the answer names the code paths that drive a
	// mechanism, which is the question that matters when the mechanism works in one context and
	// not in another. Read-only.
	[[nodiscard]] std::string CallSitesOf(std::uintptr_t a_targetOffset);

	// Where a function's ADDRESS appears as data - a vtable slot or a function-pointer table.
	// A function with no direct callers is reached indirectly, and this is what finds the table it
	// is reached through. Read-only.
	[[nodiscard]] std::string DataRefsTo(std::uintptr_t a_targetOffset);

	// Hand back raw bytes from the running module, as hex, for disassembly outside the game.
	// The on-disk exe is Steam-packed, so this is the only place those bytes exist (PREFLIGHT).
	// Read-only, bounded, and a test hook - nothing the page uses.
	[[nodiscard]] std::string DumpBytes(std::uintptr_t a_offset, std::size_t a_length);

	// Ask the game to open one of its own menus by name, so the scene can be read against a menu
	// that genuinely renders it. Main-thread work, queued like everything else. This is a test
	// hook for the driving tool, not something the page uses.
	void OpenGameMenu(const std::string& a_menuName);
}
