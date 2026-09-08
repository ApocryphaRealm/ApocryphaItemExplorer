#include "PCH.h"

#include "Catalog.h"

#include "utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_map>

namespace Catalog
{
	namespace
	{
		std::vector<Plugin> g_plugins;
		std::vector<Item>   g_items;
		bool                g_built = false;

		[[nodiscard]] std::string Lower(std::string_view a_in)
		{
			std::string out(a_in);
			std::transform(out.begin(), out.end(), out.begin(),
						   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		[[nodiscard]] bool Contains(std::string_view a_haystackLower, std::string_view a_needleLower)
		{
			return a_needleLower.empty() || a_haystackLower.find(a_needleLower) != std::string_view::npos;
		}

		// Every form carries the file that FIRST defined it at index 0. That is the plugin a player
		// means when they say "the items this mod adds" - not whoever last edited the record.
		[[nodiscard]] const RE::TESFile* OwningFile(const RE::TESForm* a_form)
		{
			return a_form ? a_form->GetFile(0) : nullptr;
		}

		template <class T>
		void Collect(Kind a_kind, const std::unordered_map<std::string, std::uint32_t>& a_indexOf)
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler) { return; }

			std::size_t added = 0;
			for (T* form : handler->GetFormArray<T>())
			{
				if (!form) { continue; }

				const RE::TESFile* file = OwningFile(form);
				if (!file) { continue; }

				const auto it = a_indexOf.find(std::string(file->GetFilename()));
				if (it == a_indexOf.end()) { continue; }

				Item item{};
				item.form = form;
				item.formID = form->GetFormID();
				item.kind = a_kind;
				item.pluginIndex = it->second;

				const char* name = form->GetName();
				item.name = (name && name[0]) ? name : "";

				const char* edid = form->GetFormEditorID();
				item.editorID = (edid && edid[0]) ? edid : "";

				// A form with neither a name nor an editor ID is not something a player can
				// meaningfully pick out of a list, so it is skipped rather than shown as blank.
				if (item.name.empty() && item.editorID.empty()) { continue; }

				item.weight = form->GetWeight();
				item.value = form->GetGoldValue();

				g_items.push_back(std::move(item));
				++added;
			}

			logger::debug("catalog: {} {}(s)", added, KindName(a_kind));
		}
	}

	const char* KindName(Kind a_kind)
	{
		switch (a_kind)
		{
		case Kind::kWeapon:     return "Weapon";
		case Kind::kArmor:      return "Armor";
		case Kind::kAmmo:       return "Ammo";
		case Kind::kBook:       return "Book";
		case Kind::kIngredient: return "Ingredient";
		case Kind::kPotion:     return "Potion";
		case Kind::kScroll:     return "Scroll";
		case Kind::kSoulGem:    return "Soul gem";
		case Kind::kKey:        return "Key";
		case Kind::kMisc:       return "Misc";
		case Kind::kLight:      return "Light";
		case Kind::kSpell:      return "Spell";
		default:                return "Unknown";
		}
	}

	bool Built() { return g_built; }

	const std::vector<Plugin>& Plugins() { return g_plugins; }
	const std::vector<Item>&   Items()   { return g_items; }

	std::size_t Build()
	{
		g_plugins.clear();
		g_items.clear();
		g_built = false;

		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler)
		{
			logger::error("catalog: TESDataHandler::GetSingleton() returned null");
			return 0;
		}

		// The full plugins first, then the light ones, which is the order the game itself keeps.
		std::unordered_map<std::string, std::uint32_t> indexOf;

		const auto add = [&](const RE::TESFile* a_file, bool a_light, std::uint32_t a_index) {
			if (!a_file) { return; }
			Plugin p{};
			p.fileName = std::string(a_file->GetFilename());
			p.light = a_light;
			p.index = a_index;
			p.itemCount = 0;
			indexOf.emplace(p.fileName, static_cast<std::uint32_t>(g_plugins.size()));
			g_plugins.push_back(std::move(p));
		};

		const auto* const* mods = handler->GetLoadedMods();
		const std::uint8_t modCount = handler->GetLoadedModCount();
		for (std::uint8_t i = 0; i < modCount; ++i) { add(mods[i], false, i); }

		const auto* const* light = handler->GetLoadedLightMods();
		const std::uint16_t lightCount = handler->GetLoadedLightModCount();
		for (std::uint16_t i = 0; i < lightCount; ++i) { add(light[i], true, i); }

		logger::info("catalog: {} plugin(s) loaded ({} full, {} light)",
					 g_plugins.size(), modCount, lightCount);

		Collect<RE::TESObjectWEAP>(Kind::kWeapon, indexOf);
		Collect<RE::TESObjectARMO>(Kind::kArmor, indexOf);
		Collect<RE::TESAmmo>(Kind::kAmmo, indexOf);
		Collect<RE::TESObjectBOOK>(Kind::kBook, indexOf);
		Collect<RE::IngredientItem>(Kind::kIngredient, indexOf);
		Collect<RE::AlchemyItem>(Kind::kPotion, indexOf);
		Collect<RE::ScrollItem>(Kind::kScroll, indexOf);
		Collect<RE::TESSoulGem>(Kind::kSoulGem, indexOf);
		Collect<RE::TESKey>(Kind::kKey, indexOf);
		Collect<RE::TESObjectMISC>(Kind::kMisc, indexOf);
		Collect<RE::TESObjectLIGH>(Kind::kLight, indexOf);
		Collect<RE::SpellItem>(Kind::kSpell, indexOf);

		for (const Item& item : g_items)
		{
			if (item.pluginIndex < g_plugins.size()) { ++g_plugins[item.pluginIndex].itemCount; }
		}

		g_built = true;
		logger::info("catalog: {} item(s) across {} plugin(s)", g_items.size(), g_plugins.size());
		return g_items.size();
	}

	std::vector<const Item*> ItemsOf(std::uint32_t a_pluginIndex, std::string_view a_search,
									 bool a_kindFilter[static_cast<std::size_t>(Kind::kCount)])
	{
		std::vector<const Item*> out;
		const std::string needle = Lower(a_search);

		for (const Item& item : g_items)
		{
			if (item.pluginIndex != a_pluginIndex) { continue; }
			if (a_kindFilter && !a_kindFilter[static_cast<std::size_t>(item.kind)]) { continue; }
			if (!needle.empty() &&
				!Contains(Lower(item.name), needle) && !Contains(Lower(item.editorID), needle))
			{
				continue;
			}
			out.push_back(&item);
		}
		return out;
	}

	std::vector<const Item*> SearchAll(std::string_view a_search,
									   bool a_kindFilter[static_cast<std::size_t>(Kind::kCount)],
									   std::size_t a_limit)
	{
		std::vector<const Item*> out;
		const std::string needle = Lower(a_search);
		if (needle.empty()) { return out; }

		for (const Item& item : g_items)
		{
			if (a_kindFilter && !a_kindFilter[static_cast<std::size_t>(item.kind)]) { continue; }
			if (!Contains(Lower(item.name), needle) && !Contains(Lower(item.editorID), needle)) { continue; }
			out.push_back(&item);
			if (a_limit && out.size() >= a_limit) { break; }
		}
		return out;
	}

	void GiveToPlayer(RE::TESForm* a_form, std::uint32_t a_count)
	{
		if (!a_form || a_count == 0) { return; }

		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks)
		{
			logger::error("catalog: no task interface; cannot add an item from the render thread");
			return;
		}

		// Never touch the player from the UI thread - queue it onto the main thread, the same rule
		// the rest of this line follows.
		tasks->AddTask([a_form, a_count]() {
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) { return; }

			if (auto* spell = a_form->As<RE::SpellItem>())
			{
				player->AddSpell(spell);
				logger::info("catalog: taught the spell \"{}\" (0x{:08X})",
							 spell->GetName() ? spell->GetName() : "?", spell->GetFormID());
				return;
			}

			auto* bound = a_form->As<RE::TESBoundObject>();
			if (!bound)
			{
				logger::warn("catalog: 0x{:08X} is not a bound object and not a spell; nothing added",
							 a_form->GetFormID());
				return;
			}

			player->AddObjectToContainer(bound, nullptr, static_cast<std::int32_t>(a_count), nullptr);
			logger::info("catalog: added {} x \"{}\" (0x{:08X})", a_count,
						 bound->GetName() ? bound->GetName() : "?", bound->GetFormID());
		});
	}
}
