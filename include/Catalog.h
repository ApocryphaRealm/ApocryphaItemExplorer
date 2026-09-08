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
	};

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
												   bool a_kindFilter[static_cast<std::size_t>(Kind::kCount)]);

	// Search every plugin at once - the thing the original gates behind a keypress inside a list.
	[[nodiscard]] std::vector<const Item*> SearchAll(std::string_view a_search,
													 bool a_kindFilter[static_cast<std::size_t>(Kind::kCount)],
													 std::size_t a_limit);

	// Put a_count of a_form into the player's inventory, on the main thread, through the game's
	// own path so the item arrives exactly as a container would hand it over.
	void GiveToPlayer(RE::TESForm* a_form, std::uint32_t a_count);
}
