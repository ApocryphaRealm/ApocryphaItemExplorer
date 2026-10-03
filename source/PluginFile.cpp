#include "PCH.h"

#include "PluginFile.h"

#include "utils/Logger.h"

#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <vector>

namespace pluginfile
{
	namespace
	{
		// The 24-byte header every record and every group starts with. For a record: type, data size, flags, form id,
		// version-control info, form version, unknown. For a GRUP: "GRUP", the group's whole size (header included),
		// its label (a record type for a top-level group), its group type (0 = top level), then stamps.
		struct Header
		{
			char          type[4];
			std::uint32_t size;
			std::uint32_t a;   // record: flags          GRUP: label
			std::uint32_t b;   // record: form id        GRUP: group type
			std::uint32_t c;
			std::uint32_t d;
		};
		static_assert(sizeof(Header) == 24);

		constexpr std::uint32_t kCompressed = 0x00040000;

		// Sanity bounds: a size past these is not a real record, it is a misread, and reading on would only produce
		// garbage (logic library 49 - a number that cannot be true is a bug in the reader).
		constexpr std::uint32_t kMaxHeaderData = 64u * 1024u * 1024u;
		constexpr std::uint32_t kMaxItemRecord = 16u * 1024u * 1024u;
		constexpr std::size_t   kMaxEditorID = 512;

		// The record types Catalog::Build collects, apart from spells (SPEL - nameless spells are never listed).
		constexpr std::array<const char*, 11> kItemTypes = { "WEAP", "ARMO", "AMMO", "BOOK", "INGR", "ALCH",
															  "SCRL", "SLGM", "KEYM", "MISC", "LIGH" };

		[[nodiscard]] bool IsItemType(const char* a_four)
		{
			for (const char* t : kItemTypes)
			{
				if (std::memcmp(t, a_four, 4) == 0) { return true; }
			}
			return false;
		}

		[[nodiscard]] bool IsItemType(std::uint32_t a_label)
		{
			char four[4];
			std::memcpy(four, &a_label, 4);
			return IsItemType(four);
		}

		// Walks a record's subrecords (type, 16-bit size, data; an XXXX subrecord carries the 32-bit size of the one
		// after it, whose own size field is then 0) and returns the EDID, printable ASCII only - an editor ID is an
		// identifier, and anything else would only reach ImGui and the DevBench JSON as broken text.
		[[nodiscard]] std::optional<std::string> FindEditorID(const std::vector<char>& a_data)
		{
			std::size_t   i = 0;
			std::uint32_t bigSize = 0;
			bool          haveBig = false;
			while (i + 6 <= a_data.size())
			{
				const char* type = a_data.data() + i;
				std::uint16_t size16 = 0;
				std::memcpy(&size16, a_data.data() + i + 4, 2);
				i += 6;
				std::size_t size = size16;
				if (haveBig) { size = bigSize; haveBig = false; }
				if (i + size > a_data.size()) { return std::nullopt; }

				if (std::memcmp(type, "XXXX", 4) == 0 && size >= 4)
				{
					std::memcpy(&bigSize, a_data.data() + i, 4);
					haveBig = true;
				}
				else if (std::memcmp(type, "EDID", 4) == 0)
				{
					std::string out;
					for (std::size_t k = 0; k < size && k < kMaxEditorID; ++k)
					{
						const auto ch = static_cast<unsigned char>(a_data[i + k]);
						if (ch == 0) { break; }
						out += (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : '?';
					}
					if (out.empty()) { return std::nullopt; }
					return out;
				}
				i += size;
			}
			return std::nullopt;
		}

		void ReadFile(const std::filesystem::path& a_path, std::uint64_t a_fileSize, bool a_light, EditorIDs& a_out)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in)
			{
				a_out.error = "could not be opened";
				return;
			}

			Header h{};
			if (!in.read(reinterpret_cast<char*>(&h), sizeof(h)) || std::memcmp(h.type, "TES4", 4) != 0)
			{
				a_out.error = "has no TES4 header";
				return;
			}
			if (h.size > kMaxHeaderData || 24ull + h.size > a_fileSize)
			{
				a_out.error = "has a TES4 header larger than the file";
				return;
			}
			{
				std::vector<char> header(h.size);
				if (h.size && !in.read(header.data(), h.size))
				{
					a_out.error = "could not read its TES4 header";
					return;
				}
				// Masters: one MAST subrecord each. The header is never compressed.
				std::size_t   i = 0;
				std::uint32_t bigSize = 0;
				bool          haveBig = false;
				while (i + 6 <= header.size())
				{
					const char* type = header.data() + i;
					std::uint16_t size16 = 0;
					std::memcpy(&size16, header.data() + i + 4, 2);
					i += 6;
					std::size_t size = size16;
					if (haveBig) { size = bigSize; haveBig = false; }
					if (std::memcmp(type, "XXXX", 4) == 0 && size >= 4 && i + 4 <= header.size())
					{
						std::memcpy(&bigSize, header.data() + i, 4);
						haveBig = true;
					}
					else if (std::memcmp(type, "MAST", 4) == 0)
					{
						++a_out.masters;
					}
					i += size;
				}
			}
			a_out.opened = true;

			// A plain linear walk: entering a group is just reading on past its header, because a group's contents
			// follow it directly. Only a TOP-LEVEL group whose record type the catalogue does not list is skipped
			// whole - which is every cell, worldspace, quest and dialogue tree, i.e. nearly all of a big master.
			std::vector<char> data;
			std::uint64_t     pos = 24ull + h.size;
			while (pos + 24 <= a_fileSize)
			{
				in.seekg(static_cast<std::streamoff>(pos));
				if (!in.read(reinterpret_cast<char*>(&h), sizeof(h)))
				{
					a_out.error = "ended inside a header";
					return;
				}

				if (std::memcmp(h.type, "GRUP", 4) == 0)
				{
					if (h.size < 24 || pos + h.size > a_fileSize)
					{
						a_out.error = "has a group whose size runs past the end of the file";
						return;
					}
					const auto groupType = static_cast<std::int32_t>(h.b);
					if (groupType == 0 && !IsItemType(h.a))
					{
						pos += h.size;   // not an item group: skip it and everything in it
					}
					else
					{
						pos += 24;       // step inside
					}
					continue;
				}

				const std::uint64_t end = pos + 24 + h.size;
				if (end > a_fileSize)
				{
					a_out.error = "has a record whose size runs past the end of the file";
					return;
				}

				// Only the file's OWN item records: an override of a master's record has a lower top byte.
				if ((h.b >> 24) == a_out.masters && IsItemType(h.type))
				{
					++a_out.records;
					if (h.a & kCompressed)
					{
						++a_out.compressed;   // zlib is not linked into this mod: counted, logged, skipped
					}
					else if (h.size > 0 && h.size <= kMaxItemRecord)
					{
						data.resize(h.size);
						if (!in.read(data.data(), h.size))
						{
							a_out.error = "could not read a record";
							return;
						}
						if (auto edid = FindEditorID(data))
						{
							a_out.byLocalID.emplace(LocalID(h.b, a_light), std::move(*edid));
						}
					}
				}
				pos = end;
			}
		}

		struct CacheEntry
		{
			std::uint64_t                   size = 0;
			std::filesystem::file_time_type written{};
			bool                            light = false;
			EditorIDs                       ids;
		};

		std::mutex                                  g_lock;
		std::unordered_map<std::string, CacheEntry> g_cache;   // lower-cased file name -> answer
	}

	const EditorIDs& Read(std::string_view a_fileName, bool a_light)
	{
		std::lock_guard lock(g_lock);

		std::string key(a_fileName);
		for (char& c : key) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

		std::error_code ec;
		const std::filesystem::path path = std::filesystem::current_path(ec) / "Data" / std::string(a_fileName);
		const std::uint64_t size = std::filesystem::file_size(path, ec);
		const bool          haveSize = !ec;
		const auto          written = haveSize ? std::filesystem::last_write_time(path, ec) : std::filesystem::file_time_type{};

		// unordered_map nodes never move, so the reference handed back stays valid until this entry is replaced.
		CacheEntry& entry = g_cache[key];
		if (haveSize && !ec && entry.ids.opened && entry.size == size && entry.written == written && entry.light == a_light)
		{
			entry.ids.fromCache = true;
			entry.ids.milliseconds = 0.0;
			logger::debug("plugin file: {} - {} editor ID(s) from the cache", a_fileName, entry.ids.byLocalID.size());
			return entry.ids;
		}

		entry = CacheEntry{};
		entry.size = haveSize ? size : 0;
		entry.written = written;
		entry.light = a_light;

		const auto start = std::chrono::steady_clock::now();
		if (!haveSize)
		{
			entry.ids.error = "was not found under Data";
		}
		else
		{
			ReadFile(path, size, a_light, entry.ids);
		}
		entry.ids.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

		logger::debug("plugin file: {} - {} master(s), {} own item record(s), {} editor ID(s), {} compressed (skipped), "
					  "{:.1f} ms{}{}",
					  a_fileName, entry.ids.masters, entry.ids.records, entry.ids.byLocalID.size(), entry.ids.compressed,
					  entry.ids.milliseconds, entry.ids.error.empty() ? "" : " - ", entry.ids.error);
		return entry.ids;
	}
}
