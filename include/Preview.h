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
	};

	[[nodiscard]] Status GetStatus();
}
