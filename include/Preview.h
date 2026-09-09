#pragma once

// ApocryphaRealm Item Explorer - the 3D preview.
//
// The game already knows how to render exactly one inventory item's model at a time: it is what
// the inventory does when you highlight something. RE::Inventory3DManager owns that, and it takes
// a base object - which is precisely what a catalogue of forms has. So this draws nothing itself
// and copies nothing; it asks the game to show the selected item, and asks it to stop.
//
// One item at a time is not a limitation worked around, it is the shape of the underlying system.

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

	// What is being shown, or nullptr.
	[[nodiscard]] RE::TESForm* Current();

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
	};

	[[nodiscard]] Status GetStatus();
}
