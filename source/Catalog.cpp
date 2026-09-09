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

		// Plugins are discovered FROM THE FORMS, not from TESDataHandler's loaded-mod arrays.
		// Those arrays are reached through version-dependent offsets, and on this machine they
		// reported 202 full and 3,438 light plugins for a profile with ten - garbage that we then
		// stored as thousands of junk file pointers. Every form knows the file that defined it, so
		// asking the forms needs no offsets and is right on every runtime. The only thing it costs
		// is that a plugin providing no items never appears, which is what we want anyway.
		std::unordered_map<const RE::TESFile*, std::uint32_t> g_indexOf;

		[[nodiscard]] std::uint32_t PluginIndexFor(const RE::TESFile* a_file)
		{
			const auto it = g_indexOf.find(a_file);
			if (it != g_indexOf.end()) { return it->second; }

			Plugin p{};
			p.fileName = std::string(a_file->GetFilename());
			p.light = a_file->IsLight();
			p.index = a_file->GetPartialIndex();
			p.itemCount = 0;

			const auto idx = static_cast<std::uint32_t>(g_plugins.size());
			g_plugins.push_back(std::move(p));
			g_indexOf.emplace(a_file, idx);
			return idx;
		}

		template <class T>
		void Collect(Kind a_kind)
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler) { return; }

			std::size_t added = 0;
			for (T* form : handler->GetFormArray<T>())
			{
				if (!form) { continue; }

				const RE::TESFile* file = OwningFile(form);
				if (!file) { continue; }

				// A file name that is empty or absurd means the pointer is not really a TESFile;
				// skip it rather than trusting it.
				const std::string_view fileName = file->GetFilename();
				if (fileName.empty() || fileName.size() > 260) { continue; }

				Item item{};
				item.form = form;
				item.formID = form->GetFormID();
				item.kind = a_kind;

				const char* name = form->GetName();
				item.name = (name && name[0]) ? name : "";

				const char* edid = form->GetFormEditorID();
				item.editorID = (edid && edid[0]) ? edid : "";

				// A form with neither a name nor an editor ID is not something a player can pick
				// out of a list, so it is skipped rather than shown blank.
				if (item.name.empty() && item.editorID.empty()) { continue; }

				item.pluginIndex = PluginIndexFor(file);
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
		g_indexOf.clear();
		g_built = false;

		if (!RE::TESDataHandler::GetSingleton())
		{
			logger::error("catalog: TESDataHandler::GetSingleton() returned null");
			return 0;
		}

		Collect<RE::TESObjectWEAP>(Kind::kWeapon);
		Collect<RE::TESObjectARMO>(Kind::kArmor);
		Collect<RE::TESAmmo>(Kind::kAmmo);
		Collect<RE::TESObjectBOOK>(Kind::kBook);
		Collect<RE::IngredientItem>(Kind::kIngredient);
		Collect<RE::AlchemyItem>(Kind::kPotion);
		Collect<RE::ScrollItem>(Kind::kScroll);
		Collect<RE::TESSoulGem>(Kind::kSoulGem);
		Collect<RE::TESKey>(Kind::kKey);
		Collect<RE::TESObjectMISC>(Kind::kMisc);
		Collect<RE::TESObjectLIGH>(Kind::kLight);
		Collect<RE::SpellItem>(Kind::kSpell);

		for (const Item& item : g_items)
		{
			if (item.pluginIndex < g_plugins.size()) { ++g_plugins[item.pluginIndex].itemCount; }
		}

		// Plugins in load order rather than in the order items happened to be found.
		std::vector<std::uint32_t> order(g_plugins.size());
		for (std::uint32_t i = 0; i < order.size(); ++i) { order[i] = i; }
		std::sort(order.begin(), order.end(), [](std::uint32_t a, std::uint32_t b) {
			if (g_plugins[a].light != g_plugins[b].light) { return !g_plugins[a].light; }
			return g_plugins[a].index < g_plugins[b].index;
		});
		std::vector<std::uint32_t> newIndexOf(g_plugins.size());
		std::vector<Plugin> sorted;
		sorted.reserve(g_plugins.size());
		for (std::uint32_t pos = 0; pos < order.size(); ++pos)
		{
			newIndexOf[order[pos]] = pos;
			sorted.push_back(std::move(g_plugins[order[pos]]));
		}
		g_plugins = std::move(sorted);
		for (Item& item : g_items) { item.pluginIndex = newIndexOf[item.pluginIndex]; }

		std::size_t light = 0;
		for (const Plugin& p : g_plugins) { if (p.light) { ++light; } }

		g_built = true;
		logger::info("catalog: {} item(s) from {} plugin(s) that provide any ({} light)",
					 g_items.size(), g_plugins.size(), light);
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
