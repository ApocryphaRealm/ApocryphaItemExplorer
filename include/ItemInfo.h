#pragma once

// ApocryphaRealm Item Explorer - what the left-pane card says about an item (1.2.0, the owner, 2026-10-23):
// "most items have some kind of description, so display the description and its inventory information below the
// preview." The description (a book's text, a potion's or an enchantment's effects, the item's own description) and
// the inventory figures (armour rating, damage, speed, soul capacity, what a book teaches).
//
// A description is read by the engine from the plugin FILE (SE keeps none in memory), so the whole read runs on the
// main thread through SKSE's task queue when the card moves to a new item; the page draws whatever the last read
// left. Only raw values are kept here - the page formats them in the player's language.

#include <cstdint>
#include <string>
#include <vector>

namespace RE
{
	class TESForm;
}

namespace iteminfo
{
	enum class ArmorClass : std::int8_t
	{
		kNone = -1,
		kLight = 0,
		kHeavy = 1,
		kClothing = 2
	};

	struct Info
	{
		std::uint32_t formID = 0;
		bool          ready = false;           // the main-thread read has finished for formID
		std::string   description;             // the item's own text (book text tags removed), may be empty
		std::vector<std::string> effects;      // one line per magic effect, <mag>/<dur>/<area> filled in
		float         armorRating = -1.0F;     // armour only
		ArmorClass    armorClass = ArmorClass::kNone;
		int           damage = -1;             // weapons and ammunition
		float         speed = -1.0F;           // weapons
		float         reach = -1.0F;           // weapons
		int           critDamage = -1;         // weapons
		int           soulCapacity = -1;       // soul gems: 1 petty .. 5 grand
		int           soulHeld = -1;           // soul gems: 0 empty .. 5 grand
		std::string   teachesSkill;            // skill books: the skill's name as the game spells it
		std::string   teachesSpell;            // spell tomes: the spell's name
	};

	// Ask for an item's details (page thread, every frame; cheap when it is already the current one).
	void Request(RE::TESForm* a_form);

	// A copy of the last finished read. ready is false while the read for the requested item is still queued.
	[[nodiscard]] Info Current();
}
