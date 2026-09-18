#include "PCH.h"

#include "Settings.h"

#include "Catalog.h"

#include "utils/INISettingCollection.h"
#include "utils/Logger.h"
#include "utils/Setting.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace settings
{
	namespace
	{
		std::string iniPath;

		struct Defaults
		{
			std::uint32_t logLevel;
			std::uint32_t defaultCount;
			bool          includeSpells;
			bool          show3DPreview;
			bool          showQuestItems;
			std::uint32_t sortMode;
			float         paneX;
			float         paneY;
			float         paneSize;
			float         modelScale;
			float         offsetX;
			float         offsetY;
			float         backgroundR;
			float         backgroundG;
			float         backgroundB;
			float         paneAlpha;
		} defaults{};

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		std::string Trim(const std::string& a_s)
		{
			const auto b = a_s.find_first_not_of(" \t\r\n");
			if (b == std::string::npos) { return {}; }
			const auto e = a_s.find_last_not_of(" \t\r\n");
			return a_s.substr(b, e - b + 1);
		}

		bool ParseBool(const std::string& a_text, bool& a_out)
		{
			const std::string v = Lower(Trim(a_text));
			if (v == "1" || v == "true" || v == "yes") { a_out = true; return true; }
			if (v == "0" || v == "false" || v == "no") { a_out = false; return true; }
			return false;
		}

		bool ParseUInt(const std::string& a_text, std::uint32_t& a_out)
		{
			try { a_out = static_cast<std::uint32_t>(std::stoull(Trim(a_text), nullptr, 0)); return true; } catch (...) { return false; }
		}

		bool ParseFloat(const std::string& a_text, float& a_out)
		{
			try { a_out = std::stof(Trim(a_text)); return true; } catch (...) { return false; }
		}

		// Three decimals: the pane is a screen fraction, where the third decimal is still about two
		// pixels on a 1080p display, and the mapping constants are read back by the calibration
		// sweep rather than typed by hand.
		std::string FloatText(float a_value)
		{
			return std::format("{:.3f}", a_value);
		}

		std::map<std::string, std::string> ReadKeys()
		{
			std::map<std::string, std::string> keys;
			std::ifstream in(iniPath);
			if (!in) { return keys; }
			std::string line, section;
			while (std::getline(in, line))
			{
				const std::string t = Trim(line);
				if (t.empty() || t[0] == ';' || t[0] == '#') { continue; }
				if (t.front() == '[' && t.back() == ']') { section = Lower(t.substr(1, t.size() - 2)); continue; }
				const auto eq = t.find('=');
				if (eq == std::string::npos) { continue; }
				keys[Lower(Trim(t.substr(0, eq))) + ":" + section] = Trim(t.substr(eq + 1));
			}
			return keys;
		}

		bool LoadFileValues()
		{
			if (!std::filesystem::exists(iniPath))
			{
				logger::warn("INI not found at {}; keeping compiled defaults", iniPath);
				return false;
			}
			const auto k = ReadKeys();
			auto get = [&](const char* a_key, auto& a_out, auto a_parse) {
				const auto it = k.find(a_key);
				if (it == k.end()) { logger::debug("INI key {} missing; keeping current value", a_key); return; }
				if (!a_parse(it->second, a_out)) { logger::warn("INI value \"{}\" for {} is not valid; keeping current value", it->second, a_key); }
			};
			get("uloglevel:debug", debug::logLevel, ParseUInt);
			get("udefaultcount:general", general::defaultCount, ParseUInt);
			get("bincludespells:general", general::includeSpells, ParseBool);
			get("bshow3dpreview:general", general::show3DPreview, ParseBool);
			get("bshowquestitems:general", general::showQuestItems, ParseBool);
			get("usortmode:general", general::sortMode, ParseUInt);
			get("fpanex:preview", preview::paneX, ParseFloat);
			get("fpaney:preview", preview::paneY, ParseFloat);
			get("fpanesize:preview", preview::paneSize, ParseFloat);
			get("fmodelscale:preview", preview::modelScale, ParseFloat);
			get("foffsetx:preview", preview::offsetX, ParseFloat);
			get("foffsety:preview", preview::offsetY, ParseFloat);
			get("fbackgroundr:preview", preview::backgroundR, ParseFloat);
			get("fbackgroundg:preview", preview::backgroundG, ParseFloat);
			get("fbackgroundb:preview", preview::backgroundB, ParseFloat);
			get("fpanealpha:preview", preview::paneAlpha, ParseFloat);
			if (general::defaultCount == 0) { general::defaultCount = 1; }
			preview::paneX = std::clamp(preview::paneX, 0.05F, 0.95F);
			preview::paneY = std::clamp(preview::paneY, 0.05F, 0.95F);
			preview::paneSize = std::clamp(preview::paneSize, 0.08F, 0.90F);
			preview::modelScale = std::clamp(preview::modelScale, 0.2F, 4.0F);
			preview::backgroundR = std::clamp(preview::backgroundR, 0.0F, 1.0F);
			preview::backgroundG = std::clamp(preview::backgroundG, 0.0F, 1.0F);
			preview::backgroundB = std::clamp(preview::backgroundB, 0.0F, 1.0F);
			preview::paneAlpha = std::clamp(preview::paneAlpha, 0.1F, 1.0F);
			// Clamped rather than trusted: an out-of-range sort index read from a hand-edited INI
			// would index past the end of the mode table.
			if (general::sortMode >= static_cast<std::uint32_t>(Catalog::Sort::kCount)) { general::sortMode = 0; }
			logger::info("settings loaded from {}: defaultCount={} includeSpells={} preview3D={} "
						 "questItems={} sortMode={} logLevel={}",
						 iniPath, general::defaultCount, general::includeSpells, general::show3DPreview,
						 general::showQuestItems, general::sortMode, debug::logLevel);
			logger::info("preview box: centre ({:.2f}, {:.2f}) size {:.2f}, model scale {:.2f}, offset ({:.0f}, {:.0f})",
						 preview::paneX, preview::paneY, preview::paneSize, preview::modelScale, preview::offsetX, preview::offsetY);
			return true;
		}

		bool WriteKey(std::vector<std::string>& a_lines, const char* a_section, const char* a_key, const std::string& a_value)
		{
			const std::string wantSection = Lower(a_section);
			const std::string wantKey = Lower(a_key);
			std::string section;
			for (auto& line : a_lines)
			{
				const std::string t = Trim(line);
				if (!t.empty() && t.front() == '[' && t.back() == ']') { section = Lower(t.substr(1, t.size() - 2)); continue; }
				const auto eq = t.find('=');
				if (eq == std::string::npos || section != wantSection) { continue; }
				if (Lower(Trim(t.substr(0, eq))) == wantKey)
				{
					line = std::string(a_key) + "=" + a_value;
					return true;
				}
			}
			logger::warn("Save: key {} not found in [{}]", a_key, a_section);
			return false;
		}
	}

	void Init(const std::string& a_iniFileName)
	{
		iniPath = (std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / a_iniFileName).string();

		defaults = { debug::logLevel, general::defaultCount, general::includeSpells,
					 general::show3DPreview, general::showQuestItems, general::sortMode,
					 preview::paneX, preview::paneY, preview::paneSize, preview::modelScale, preview::offsetX, preview::offsetY,
					 preview::backgroundR, preview::backgroundG, preview::backgroundB, preview::paneAlpha };

		auto* collection = utils::INISettingCollection::GetSingleton();
		collection->AddSettings(
			utils::MakeSetting("uLogLevel:Debug", static_cast<unsigned int>(debug::logLevel)),
			utils::MakeSetting("uDefaultCount:General", static_cast<unsigned int>(general::defaultCount)),
			utils::MakeSetting("bIncludeSpells:General", general::includeSpells),
			utils::MakeSetting("bShow3DPreview:General", general::show3DPreview),
			utils::MakeSetting("bShowQuestItems:General", general::showQuestItems),
			utils::MakeSetting("uSortMode:General", static_cast<unsigned int>(general::sortMode)),
			utils::MakeSetting("fPaneX:Preview", preview::paneX),
			utils::MakeSetting("fPaneY:Preview", preview::paneY),
			utils::MakeSetting("fPaneSize:Preview", preview::paneSize),
			utils::MakeSetting("fModelScale:Preview", preview::modelScale),
			utils::MakeSetting("fOffsetX:Preview", preview::offsetX),
			utils::MakeSetting("fOffsetY:Preview", preview::offsetY),
			utils::MakeSetting("fBackgroundR:Preview", preview::backgroundR),
			utils::MakeSetting("fBackgroundG:Preview", preview::backgroundG),
			utils::MakeSetting("fBackgroundB:Preview", preview::backgroundB),
			utils::MakeSetting("fPaneAlpha:Preview", preview::paneAlpha));

		LoadFileValues();
	}

	bool Reload()
	{
		const bool ok = LoadFileValues();
		ApplyLogLevel();
		return ok;
	}

	bool Save()
	{
		std::vector<std::string> lines;
		{
			std::ifstream in(iniPath);
			if (!in) { logger::error("Save: could not open {} for reading", iniPath); return false; }
			std::string line;
			while (std::getline(in, line)) { lines.push_back(line); }
		}

		bool ok = true;
		ok &= WriteKey(lines, "Debug", "uLogLevel", std::to_string(debug::logLevel));
		ok &= WriteKey(lines, "General", "uDefaultCount", std::to_string(general::defaultCount));
		ok &= WriteKey(lines, "General", "bIncludeSpells", general::includeSpells ? "1" : "0");
		ok &= WriteKey(lines, "General", "bShow3DPreview", general::show3DPreview ? "1" : "0");
		ok &= WriteKey(lines, "General", "bShowQuestItems", general::showQuestItems ? "1" : "0");
		ok &= WriteKey(lines, "General", "uSortMode", std::to_string(general::sortMode));
		ok &= WriteKey(lines, "Preview", "fPaneX", FloatText(preview::paneX));
		ok &= WriteKey(lines, "Preview", "fPaneY", FloatText(preview::paneY));
		ok &= WriteKey(lines, "Preview", "fPaneSize", FloatText(preview::paneSize));
		ok &= WriteKey(lines, "Preview", "fModelScale", FloatText(preview::modelScale));
		ok &= WriteKey(lines, "Preview", "fOffsetX", FloatText(preview::offsetX));
		ok &= WriteKey(lines, "Preview", "fOffsetY", FloatText(preview::offsetY));
		ok &= WriteKey(lines, "Preview", "fBackgroundR", FloatText(preview::backgroundR));
		ok &= WriteKey(lines, "Preview", "fBackgroundG", FloatText(preview::backgroundG));
		ok &= WriteKey(lines, "Preview", "fBackgroundB", FloatText(preview::backgroundB));
		ok &= WriteKey(lines, "Preview", "fPaneAlpha", FloatText(preview::paneAlpha));

		std::ofstream out(iniPath, std::ios::trunc);
		if (!out) { logger::error("Save: could not open {} for writing", iniPath); return false; }
		for (const auto& line : lines) { out << line << '\n'; }
		logger::info("settings saved to {}", iniPath);
		return ok;
	}

	void RestoreDefaults()
	{
		debug::logLevel = defaults.logLevel;
		general::defaultCount = defaults.defaultCount;
		general::includeSpells = defaults.includeSpells;
		general::show3DPreview = defaults.show3DPreview;
		general::showQuestItems = defaults.showQuestItems;
		general::sortMode = defaults.sortMode;
		preview::paneX = defaults.paneX;
		preview::paneY = defaults.paneY;
		preview::paneSize = defaults.paneSize;
		preview::modelScale = defaults.modelScale;
		preview::offsetX = defaults.offsetX;
		preview::offsetY = defaults.offsetY;
		preview::backgroundR = defaults.backgroundR;
		preview::backgroundG = defaults.backgroundG;
		preview::backgroundB = defaults.backgroundB;
		preview::paneAlpha = defaults.paneAlpha;
		ApplyLogLevel();
	}

	void ApplyLogLevel()
	{
		const auto lvl = static_cast<spdlog::level::level_enum>(std::clamp<std::uint32_t>(debug::logLevel, 0u, 6u));
		SKSE::log::set_level(lvl, lvl);
	}

	const std::string& GetIniPath() { return iniPath; }
}
