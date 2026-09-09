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
			bool          showFrame;
			float         mapDepth;
			float         mapBaseY;
			float         mapBaseZ;
			float         mapSpanX;
			float         mapSpanY;
			float         mapScale;
			bool          overrideCamera;
			float         camFov;
			float         camX;
			float         camY;
			float         camZ;
			std::uint32_t scheme;
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
			get("bshowframe:preview", preview::showFrame, ParseBool);
			get("fmapdepth:preview", preview::mapDepth, ParseFloat);
			get("fmapbasey:preview", preview::mapBaseY, ParseFloat);
			get("fmapbasez:preview", preview::mapBaseZ, ParseFloat);
			get("fmapspanx:preview", preview::mapSpanX, ParseFloat);
			get("fmapspany:preview", preview::mapSpanY, ParseFloat);
			get("fmapscale:preview", preview::mapScale, ParseFloat);
			get("boverridecamera:preview", preview::overrideCamera, ParseBool);
			get("fcamfov:preview", preview::camFov, ParseFloat);
			get("fcamx:preview", preview::camX, ParseFloat);
			get("fcamy:preview", preview::camY, ParseFloat);
			get("fcamz:preview", preview::camZ, ParseFloat);
			get("uscheme:preview", preview::scheme, ParseUInt);
			if (preview::scheme > 7) { preview::scheme = 1; }
			if (general::defaultCount == 0) { general::defaultCount = 1; }
			// Clamped for the same reason SetPane clamps: a pane centred off-screen or sized to
			// nothing is a preview nobody can find, and it looks exactly like a broken renderer.
			preview::paneX = std::clamp(preview::paneX, 0.05F, 0.95F);
			preview::paneY = std::clamp(preview::paneY, 0.05F, 0.95F);
			preview::paneSize = std::clamp(preview::paneSize, 0.08F, 0.90F);
			// Clamped rather than trusted: an out-of-range sort index read from a hand-edited INI
			// would index past the end of the mode table.
			if (general::sortMode >= static_cast<std::uint32_t>(Catalog::Sort::kCount)) { general::sortMode = 0; }
			logger::info("settings loaded from {}: defaultCount={} includeSpells={} preview3D={} "
						 "questItems={} sortMode={} logLevel={}",
						 iniPath, general::defaultCount, general::includeSpells, general::show3DPreview,
						 general::showQuestItems, general::sortMode, debug::logLevel);
			logger::info("preview pane: ({:.3f}, {:.3f}) size {:.3f} frame={} | mapping depth={:.1f} "
						 "base=({:.1f}, {:.1f}) span=({:.1f}, {:.1f}) scale={:.3f}",
						 preview::paneX, preview::paneY, preview::paneSize, preview::showFrame,
						 preview::mapDepth, preview::mapBaseY, preview::mapBaseZ,
						 preview::mapSpanX, preview::mapSpanY, preview::mapScale);
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
					 preview::paneX, preview::paneY, preview::paneSize, preview::showFrame,
					 preview::mapDepth, preview::mapBaseY, preview::mapBaseZ,
					 preview::mapSpanX, preview::mapSpanY, preview::mapScale,
					 preview::overrideCamera, preview::camFov, preview::camX, preview::camY,
					 preview::camZ, preview::scheme };

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
			utils::MakeSetting("bShowFrame:Preview", preview::showFrame),
			utils::MakeSetting("fMapDepth:Preview", preview::mapDepth),
			utils::MakeSetting("fMapBaseY:Preview", preview::mapBaseY),
			utils::MakeSetting("fMapBaseZ:Preview", preview::mapBaseZ),
			utils::MakeSetting("fMapSpanX:Preview", preview::mapSpanX),
			utils::MakeSetting("fMapSpanY:Preview", preview::mapSpanY),
			utils::MakeSetting("fMapScale:Preview", preview::mapScale),
			utils::MakeSetting("bOverrideCamera:Preview", preview::overrideCamera),
			utils::MakeSetting("fCamFov:Preview", preview::camFov),
			utils::MakeSetting("fCamX:Preview", preview::camX),
			utils::MakeSetting("fCamY:Preview", preview::camY),
			utils::MakeSetting("fCamZ:Preview", preview::camZ),
			utils::MakeSetting("uScheme:Preview", static_cast<unsigned int>(preview::scheme)));

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
		ok &= WriteKey(lines, "Preview", "bShowFrame", preview::showFrame ? "1" : "0");
		ok &= WriteKey(lines, "Preview", "fMapDepth", FloatText(preview::mapDepth));
		ok &= WriteKey(lines, "Preview", "fMapBaseY", FloatText(preview::mapBaseY));
		ok &= WriteKey(lines, "Preview", "fMapBaseZ", FloatText(preview::mapBaseZ));
		ok &= WriteKey(lines, "Preview", "fMapSpanX", FloatText(preview::mapSpanX));
		ok &= WriteKey(lines, "Preview", "fMapSpanY", FloatText(preview::mapSpanY));
		ok &= WriteKey(lines, "Preview", "fMapScale", FloatText(preview::mapScale));
		ok &= WriteKey(lines, "Preview", "bOverrideCamera", preview::overrideCamera ? "1" : "0");
		ok &= WriteKey(lines, "Preview", "fCamFov", FloatText(preview::camFov));
		ok &= WriteKey(lines, "Preview", "fCamX", FloatText(preview::camX));
		ok &= WriteKey(lines, "Preview", "fCamY", FloatText(preview::camY));
		ok &= WriteKey(lines, "Preview", "fCamZ", FloatText(preview::camZ));
		ok &= WriteKey(lines, "Preview", "uScheme", std::to_string(preview::scheme));

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
		preview::showFrame = defaults.showFrame;
		preview::mapDepth = defaults.mapDepth;
		preview::mapBaseY = defaults.mapBaseY;
		preview::mapBaseZ = defaults.mapBaseZ;
		preview::mapSpanX = defaults.mapSpanX;
		preview::mapSpanY = defaults.mapSpanY;
		preview::mapScale = defaults.mapScale;
		preview::overrideCamera = defaults.overrideCamera;
		preview::camFov = defaults.camFov;
		preview::camX = defaults.camX;
		preview::camY = defaults.camY;
		preview::camZ = defaults.camZ;
		preview::scheme = defaults.scheme;
		ApplyLogLevel();
	}

	void ApplyLogLevel()
	{
		const auto lvl = static_cast<spdlog::level::level_enum>(std::clamp<std::uint32_t>(debug::logLevel, 0u, 6u));
		SKSE::log::set_level(lvl, lvl);
	}

	const std::string& GetIniPath() { return iniPath; }
}
