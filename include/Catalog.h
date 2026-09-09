#pragma once

// ApocryphaRealm Item Explorer - the catalogue.
//
// Every plugin the game loaded, and every item each one provides, read straight out of
// TESDataHandler. Built once on demand and cached, because walking every form array is cheap in
// C++ but not free, and the answer only changes when the load order does.
//
// Own code, MIT. Inspired by AddItemMenu - Ultimate Mod Explorer by towawot; no code or assets
// from that mod are used, and its Papyrus sources were deliberately never opened.

#include <cstdint>
#include <string>
#include <vector>

namespace RE
{
	class TESForm;
}

namespace Catalog
{
	// The kinds a container would show, in the order the page lists them.
	enum class Kind : std::uint32_t
	{
		kWeapon = 0,
		kArmor,
		kAmmo,
		kBook,
		kIngredient,
		kPotion,
		kScroll,
		kSoulGem,
		kKey,
		kMisc,
		kLight,
		kSpell,
		kCount
	};

	[[nodiscard]] const char* KindName(Kind a_kind);

	struct Item
	{
		RE::TESForm*  form;
		std::string   name;        // what the player would see
		std::string   editorID;    // useful when a form has no name
		std::uint32_t formID;
		Kind          kind;
		std::uint32_t pluginIndex; // index into Plugins()
		float         weight;
		std::int32_t  value;
		// An enchanted variant of a piece of equipment. Skyrim ships hundreds of these - every
		// "Iron Sword of Cold" is its own weapon record - and they swamp a plugin's real content,
		// so the page can switch them off.
		bool          enchanted;
		// Some quest names this exact form as a quest object. Established by ASKING the quest
		// system, not guessed: every loaded quest's aliases are walked at build time, and an alias
		// that is flagged IsQuestObject() and points at a base object marks that object here.
		// Spawning one of these can confuse the quest that owns it, so the page says so and can
		// hide them entirely.
		bool          questItem;
	};

	// How a list is ordered. The catalogue's own order is the order forms happen to sit in the
	// game's arrays, which is meaningless to a reader.
	enum class Sort : std::uint32_t
	{
		kNameAsc = 0,   // A-Z
		kNameDesc,      // Z-A
		kValueDesc,     // most valuable first
		kValueAsc,
		kWeightDesc,    // heaviest first
		kWeightAsc,
		kCount
	};

	[[nodiscard]] const char* SortName(Sort a_sort);

	struct Plugin
	{
		std::string   fileName;
		bool          light;
		std::uint32_t index;       // load index, or the light index for an ESL
		std::uint32_t itemCount;
	};

	// Build (or rebuild) the catalogue from the current load order. Safe to call again; the second
	// call throws the first away. Returns the number of items found.
	std::size_t Build();

	[[nodiscard]] bool Built();

	[[nodiscard]] const std::vector<Plugin>& Plugins();
	[[nodiscard]] const std::vector<Item>&   Items();

	// Items belonging to one plugin, optionally filtered. Both filters are case-insensitive and
	// a_search matches the name or the editor ID.
	[[nodiscard]] std::vector<const Item*> ItemsOf(std::uint32_t a_pluginIndex,
												   std::string_view a_search,
												   bool a_kindFilter[static_cast<std::size_t>(Kind::kCount)],
												   bool a_showEnchanted,
												   bool a_showQuestItems,
												   Sort a_sort);

	// Search every plugin at once - the thing the original gates behind a keypress inside a list.
	[[nodiscard]] std::vector<const Item*> SearchAll(std::string_view a_search,
													 bool a_kindFilter[static_cast<std::size_t>(Kind::kCount)],
													 bool a_showEnchanted,
													 bool a_showQuestItems,
													 Sort a_sort,
													 std::size_t a_limit);

	// Orders a list that has already been gathered. Used by the Favourites page, which builds its
	// list from saved form IDs rather than by filtering the catalogue.
	void SortItems(std::vector<const Item*>& a_items, Sort a_sort);

	// The one item matching a form, or nullptr. The Favourites page resolves saved IDs through it.
	[[nodiscard]] const Item* Find(const RE::TESForm* a_form);

	// Put a_count of a_form into the player's inventory, on the main thread, through the game's
	// own path so the item arrives exactly as a container would hand it over.
	void GiveToPlayer(RE::TESForm* a_form, std::uint32_t a_count);
}
