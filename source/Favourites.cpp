#include "PCH.h"

#include "Favourites.h"

#include "utils/Logger.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace favourites
{
	namespace
	{
		std::vector<Entry> g_entries;
		std::string        g_path;
		bool               g_resolved = false;

		const std::string& Path()
		{
			if (g_path.empty())
			{
				// Beside the log. A mod folder is not reliably writable under a mod manager, and
				// this is the one directory the plugin already knows it can write to.
				auto dir = SKSE::log::log_directory();
				g_path = dir ? (*dir / "ApocryphaItemExplorer_favourites.txt").string()
							 : std::string("ApocryphaItemExplorer_favourites.txt");
			}
			return g_path;
		}

		std::string Trim(const std::string& a_s)
		{
			const auto b = a_s.find_first_not_of(" \t\r\n");
			if (b == std::string::npos) { return {}; }
			const auto e = a_s.find_last_not_of(" \t\r\n");
			return a_s.substr(b, e - b + 1);
		}

		// The load-order byte(s) are what change between sessions; everything below is the record's
		// own identity within its plugin. 0xFE-prefixed IDs are light plugins, whose index is 12
		// bits rather than 8.
		std::uint32_t LocalID(RE::FormID a_id)
		{
			return (a_id & 0xFF000000) == 0xFE000000 ? (a_id & 0x00000FFF) : (a_id & 0x00FFFFFF);
		}
	}

	void Load()
	{
		g_entries.clear();
		g_resolved = false;

		std::ifstream in(Path());
		if (!in)
		{
			logger::debug("favourites: no list yet at {} - starting empty", Path());
			return;
		}

		std::string line;
		while (std::getline(in, line))
		{
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#') { continue; }

			// plugin.esp|0001A332|Optional remembered name
			const auto bar = line.find('|');
			if (bar == std::string::npos) { continue; }
			const auto bar2 = line.find('|', bar + 1);

			Entry e;
			e.plugin = Trim(line.substr(0, bar));
			const std::string idText = Trim(bar2 == std::string::npos ? line.substr(bar + 1)
																	  : line.substr(bar + 1, bar2 - bar - 1));
			if (bar2 != std::string::npos) { e.rememberedName = Trim(line.substr(bar2 + 1)); }

			try
			{
				e.localID = static_cast<std::uint32_t>(std::stoul(idText, nullptr, 16));
			}
			catch (...)
			{
				logger::warn("favourites: skipping a line whose id could not be read: {}", line);
				continue;
			}

			e.form = nullptr;
			if (!e.plugin.empty()) { g_entries.push_back(std::move(e)); }
		}

		logger::info("favourites: {} entr(ies) read from {}", g_entries.size(), Path());
	}

	void Resolve()
	{
		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler)
		{
			// Rule 17: not ready is not the same as not there. Leave it unresolved and try again.
			logger::debug("favourites: no data handler yet - will resolve later");
			return;
		}

		std::size_t ok = 0;
		std::vector<Entry> kept;
		kept.reserve(g_entries.size());

		for (Entry& e : g_entries)
		{
			e.form = handler->LookupForm(e.localID, e.plugin);
			if (e.form)
			{
				++ok;
				kept.push_back(std::move(e));
			}
			else
			{
				// Said out loud rather than silently dropped: this is the visible consequence of
				// the player removing a mod, and it should be explicable from the log.
				logger::info("favourites: dropping \"{}\" - {} no longer provides {:06X}",
							 e.rememberedName.empty() ? "(unnamed)" : e.rememberedName, e.plugin, e.localID);
			}
		}

		const bool lost = kept.size() != g_entries.size();
		g_entries = std::move(kept);
		g_resolved = true;

		logger::info("favourites: {} of them resolved against the current load order", ok);
		if (lost) { Save(); }
	}

	bool Save()
	{
		std::ofstream out(Path(), std::ios::trunc);
		if (!out)
		{
			logger::error("favourites: could not write {}", Path());
			return false;
		}

		out << "; ApocryphaRealm Item Explorer - favourites\n"
			<< "; plugin | local form id (hex) | the name it had when you saved it\n"
			<< "; Stored per plugin rather than by raw form id, so the list survives load-order changes.\n";

		for (const Entry& e : g_entries)
		{
			out << e.plugin << "|" << std::hex << std::uppercase << e.localID << std::dec;
			if (!e.rememberedName.empty()) { out << "|" << e.rememberedName; }
			out << "\n";
		}

		logger::debug("favourites: {} entr(ies) written", g_entries.size());
		return true;
	}

	bool Contains(const RE::TESForm* a_form)
	{
		if (!a_form) { return false; }
		return std::any_of(g_entries.begin(), g_entries.end(),
						   [&](const Entry& e) { return e.form == a_form; });
	}

	void Add(const RE::TESForm* a_form, const std::string& a_name)
	{
		if (!a_form || Contains(a_form)) { return; }

		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) { return; }

		const RE::FormID id = a_form->GetFormID();
		const auto* file = a_form->GetFile(0);
		if (!file)
		{
			// A dynamically created form has no file to name, so it cannot be written down in a
			// way that would survive a restart. Refusing is honest; pretending is not.
			logger::warn("favourites: {:08X} has no source plugin, so it cannot be saved", id);
			return;
		}

		Entry e;
		e.plugin = file->GetFilename();
		e.localID = LocalID(id);
		e.form = const_cast<RE::TESForm*>(a_form);
		e.rememberedName = a_name;
		g_entries.push_back(std::move(e));

		logger::debug("favourites: added \"{}\" ({}|{:06X})", a_name, g_entries.back().plugin, g_entries.back().localID);
		Save();
	}

	void Remove(const RE::TESForm* a_form)
	{
		const auto before = g_entries.size();
		g_entries.erase(std::remove_if(g_entries.begin(), g_entries.end(),
									   [&](const Entry& e) { return e.form == a_form; }),
						g_entries.end());
		if (g_entries.size() != before) { Save(); }
	}

	bool Toggle(const RE::TESForm* a_form, const std::string& a_name)
	{
		if (Contains(a_form))
		{
			Remove(a_form);
			return false;
		}
		Add(a_form, a_name);
		return Contains(a_form);
	}

	void Clear()
	{
		g_entries.clear();
		Save();
	}

	const std::vector<Entry>& All() { return g_entries; }

	std::size_t ResolvedCount()
	{
		return static_cast<std::size_t>(
			std::count_if(g_entries.begin(), g_entries.end(), [](const Entry& e) { return e.form != nullptr; }));
	}

	const std::string& FilePath() { return Path(); }
}
