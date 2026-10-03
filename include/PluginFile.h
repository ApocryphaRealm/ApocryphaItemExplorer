#pragma once

// ApocryphaRealm Item Explorer - editor IDs read from a plugin FILE on disk.
//
// Skyrim SE keeps no editor ID in memory for most forms, so an item with no in-game name (no FULL in its record)
// has nothing the page can show it by. Plenty of mods ship items like that - EldenSkyrim.esp ("Elden Rim") has 7
// of its 8 weapons, 32 of its 35 armour pieces and all 14 of its books nameless (binggo123, 2026-09-29: "some esps
// also cannot be displayed"). The editor ID is still in the plugin file, so when the player asks for nameless
// items (bShowUnnamed) the catalogue reads it from there.
//
// What is read: the TES4 header (to count the masters), then only the top-level groups of the item record types
// the catalogue lists - everything else (cells, worldspaces, quests, dialogue) is skipped by seeking past it, so
// even Skyrim.esm costs a few megabytes of reading. Of each item record the plugin itself defines (form id top
// byte == its master count) the EDID subrecord is kept, keyed by the LOCAL form id: the low 24 bits for a full
// plugin, the low 12 for a light one - the part of a form id that does not depend on the load order.
//
// Compressed records (flag 0x00040000) are zlib streams; this mod links no zlib, so they are counted and skipped.
//
// Own code, GPL-3.0-or-later like the rest of the mod.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace pluginfile
{
	struct EditorIDs
	{
		bool          opened = false;       // the file was found and its header read
		bool          fromCache = false;    // this answer was reused (same file name, size and write time)
		std::string   error;                // why reading stopped early or never started; empty when it read cleanly
		std::uint32_t masters = 0;          // MAST subrecords in the header - the top byte of the file's own form ids
		std::size_t   records = 0;          // own item records seen
		std::size_t   compressed = 0;       // ...of which compressed, so skipped (no zlib)
		double        milliseconds = 0.0;   // how long the read took (0 for a cached answer)
		std::unordered_map<std::uint32_t, std::string> byLocalID;   // local form id -> editor ID
	};

	// The part of a form id that is the plugin's own: low 12 bits for a light plugin, low 24 otherwise.
	[[nodiscard]] constexpr std::uint32_t LocalID(std::uint32_t a_formID, bool a_light)
	{
		return a_light ? (a_formID & 0x00000FFF) : (a_formID & 0x00FFFFFF);
	}

	// The editor IDs of every item record a_fileName defines, read from "Data\<a_fileName>" under the game's working
	// directory (which Mod Organizer's virtual file system serves). Cached by file: a second call for an unchanged
	// file (same size and write time) answers from memory. Never throws. Call it off the frame path - it reads disk.
	[[nodiscard]] const EditorIDs& Read(std::string_view a_fileName, bool a_light);
}
