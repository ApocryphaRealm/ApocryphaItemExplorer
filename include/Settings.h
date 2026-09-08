#pragma once

// ApocryphaRealm Item Explorer - settings. Plain-file INI (redirector-proof, the project standard).

#include <cstdint>
#include <string>

namespace settings
{
	namespace debug
	{
		inline std::uint32_t logLevel = 0;  // uLogLevel:Debug
	}

	namespace general
	{
		inline std::uint32_t defaultCount = 1;   // uDefaultCount:General - how many an Add gives
		inline bool includeSpells = true;        // bIncludeSpells:General - list spells alongside items
	}

	void Init(const std::string& a_iniFileName);
	bool Reload();
	bool Save();
	void RestoreDefaults();
	void ApplyLogLevel();
	const std::string& GetIniPath();
}
