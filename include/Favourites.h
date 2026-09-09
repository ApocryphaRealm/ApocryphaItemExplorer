#pragma once

// ApocryphaRealm Item Explorer - favourites.
//
// A short list of items the player marked while browsing, shown on its own page.
//
// Stored as PLUGIN NAME + LOCAL form ID, never as a raw form ID. A raw ID encodes the plugin's
// position in the load order, so it points at a different item the moment anything is installed or
// removed above it - and a favourites list that silently starts handing out the wrong things is
// worse than none. The pair is resolved back through TESDataHandler at load, and anything that no
// longer resolves is dropped with a line in the log rather than kept as a dead entry.
//
// It lives beside the log rather than in the mod folder, because a mod folder is not reliably
// writable under a mod manager, and it is deliberately NOT co-save data: a list of things you like
// the look of belongs to you, not to one save.

#include <cstdint>
#include <string>
#include <vector>

namespace RE
{
	class TESForm;
}

namespace favourites
{
	struct Entry
	{
		std::string   plugin;      // "Skyrim.esm"
		std::uint32_t localID;     // the form ID with the load-order byte stripped
		RE::TESForm*  form;        // resolved at load; null means it did not resolve
		std::string   rememberedName;  // what it was called when it was saved, for the log
	};

	// Reads the file. Safe before the data handler exists - resolution is deferred to Resolve().
	void Load();

	// Turns every entry into a form, dropping what no longer resolves. Call once the load order is
	// known (kDataLoaded).
	void Resolve();

	// Writes the file. Called after every change; the list is tiny, so there is no reason to batch.
	bool Save();

	[[nodiscard]] bool Contains(const RE::TESForm* a_form);
	void Add(const RE::TESForm* a_form, const std::string& a_name);
	void Remove(const RE::TESForm* a_form);
	// Add if absent, remove if present. Returns true if the item is favourited afterwards.
	bool Toggle(const RE::TESForm* a_form, const std::string& a_name);

	void Clear();

	[[nodiscard]] const std::vector<Entry>& All();
	[[nodiscard]] std::size_t ResolvedCount();
	[[nodiscard]] const std::string& FilePath();
}
