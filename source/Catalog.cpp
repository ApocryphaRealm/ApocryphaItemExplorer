#include "PCH.h"

#include "Catalog.h"

#include <unordered_set>

#include "utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <type_traits>
#include <functional>
#include <unordered_map>

namespace Catalog
{
	namespace
	{
		std::vector<Plugin> g_plugins;
		std::vector<Item>   g_items;
		bool                g_built = false;

		std::uint32_t g_generation = 0;

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

		// "You hold a pile of these" - the owner's own widening (2026-09-10). Potions, ingredients,
		// scrolls, ammo and soul gems by kind, plus CRAFTING MATERIALS, which are Misc records and
		// so cannot be told apart by kind alone: those are identified by the vanilla vendor keywords,
		// read off the form rather than guessed from its name. Decided here, at build time, because
		// HasKeywordString walks the form's keyword list comparing strings and the page would
		// otherwise do it for every visible row on every frame.
		// Every Misc item some crafting recipe (COBJ) uses as a material: ore and ingots, leather and strips, Hearthfire's
		// clay, nails, hinges and logs, and any modded material, without naming them one by one. Filled at the start of
		// each Build(), before the items are collected. (A form's editor id is not kept in memory, so a name prefix such as
		// "BYOHMaterial" cannot be matched - the first try at Hearthfire's materials, 2026-10-03.)
		std::unordered_set<const RE::TESForm*> g_craftingParts;

		void CollectCraftingParts()
		{
			g_craftingParts.clear();
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler) { return; }
			for (RE::BGSConstructibleObject* cobj : handler->GetFormArray<RE::BGSConstructibleObject>())
			{
				if (!cobj) { continue; }
				cobj->requiredItems.ForEachContainerObject([](RE::ContainerObject& a_entry) {
					if (a_entry.obj && a_entry.obj->Is(RE::FormType::Misc)) { g_craftingParts.insert(a_entry.obj); }
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			logger::info("catalog: {} misc item(s) are crafting materials (a recipe uses them) - each gets its own amount slider",
						 g_craftingParts.size());
		}

		[[nodiscard]] bool IsBulkItem(const Item& a_item)
		{
			// Everything you hold a pile of (the owner, 2026-10-02: "anything that is consumable or usable, like lockpicks,
			// crafting materials, ingredients, food items, and others"). Food is a potion form, so kPotion covers it; a
			// carried torch is a Light.
			switch (a_item.kind)
			{
			case Kind::kPotion:
			case Kind::kIngredient:
			case Kind::kScroll:
			case Kind::kAmmo:
			case Kind::kSoulGem:
			case Kind::kLight:
				return true;
			default:
				break;
			}
			// Lockpick (Skyrim.esm 0x0000000A): a Misc item with only VendorItemTool, which pickaxes share in spirit.
			if (a_item.formID == 0x0000000A) { return true; }
			// a material some recipe uses (Hearthfire's building materials among them - they carry no vendor keyword)
			if (a_item.kind == Kind::kMisc && a_item.form && g_craftingParts.contains(a_item.form)) { return true; }
			if (const auto* kwf = a_item.form ? a_item.form->As<RE::BGSKeywordForm>() : nullptr)
			{
				// crafting materials: ore and ingots, hides and leather, firewood, gems, and animal parts (claws,
				// feathers, dragon bone and scale), plus vendor "tools" (lockpicks in mods that add their own)
				// "VendorItemFireword" is the game's own spelling of the firewood keyword (the correct spelling never
				// matched - firewood had no amount slider before 1.1.2)
				for (const char* kw : { "VendorItemOreIngot", "VendorItemAnimalHide", "VendorItemFireword",
										"VendorItemFirewood", "VendorItemGem", "VendorItemAnimalPart", "VendorItemTool" })
				{
					if (kwf->HasKeywordString(kw)) { return true; }
				}
			}
			return false;
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
				// Engine-defined forms (form ids below 0x800: Gold001 0xF, Lockpick 0xA, the arrows and
				// skeleton keys) are created before any plugin is read, so GetFile(0) is null for them
				// although Skyrim.esm defines them; they used to be skipped (looked into when the owner,
				// 2026-09-18, at first could not find Gold: "i cant find gold in item explorer after searching
				// every plugin" - Gold itself turned out to be listed). They
				// belong to Skyrim.esm.
				if (!file && form->GetFormID() < 0x800) { file = handler->LookupModByName("Skyrim.esm"); }
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
				// SE keeps no editor id in memory for most forms, so a renamed engine item cannot be found by
				// its original name: with a mod calling gold "Septims", searching "gold" found nothing (the
				// owner, 2026-09-18: "its called septims"). The engine-defined items get their known ids.
				if (item.editorID.empty())
				{
					switch (item.formID)
					{
					case 0x0000000A: item.editorID = "Lockpick"; break;
					case 0x0000000B: item.editorID = "SkeletonKey"; break;
					case 0x0000000F: item.editorID = "Gold001"; break;
					default: break;
					}
				}

				// A form with neither a name nor an editor ID is not something a player can pick
				// out of a list, so it is skipped rather than shown blank.
				if (item.name.empty() && item.editorID.empty()) { continue; }

				item.pluginIndex = PluginIndexFor(file);
				item.weight = form->GetWeight();
				item.value = form->GetGoldValue();

				// Only weapons and armour carry an enchantment slot, so this is decided at compile
				// time rather than by a runtime cast that would be wrong for every other kind.
				if constexpr (std::is_base_of_v<RE::TESEnchantableForm, T>)
				{
					item.enchanted = (form->formEnchanting != nullptr);
				}
				else
				{
					item.enchanted = false;
				}

				// The per-frame fields, computed once (see Catalog.h). Everything the page's
				// filter and sort touch on the hot path is ready before it ever draws.
				item.nameLower = Lower(item.name);
				item.editorLower = Lower(item.editorID);
				item.sortKeyLower = item.name.empty() ? item.editorLower : item.nameLower;
				item.bulk = IsBulkItem(item);

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

	const char* SortName(Sort a_sort)
	{
		switch (a_sort)
		{
		case Sort::kNameAsc:    return "Name A-Z";
		case Sort::kNameDesc:   return "Name Z-A";
		case Sort::kValueDesc:  return "Value, highest first";
		case Sort::kValueAsc:   return "Value, lowest first";
		case Sort::kWeightDesc: return "Weight, heaviest first";
		case Sort::kWeightAsc:  return "Weight, lightest first";
		default:                return "Name A-Z";
		}
	}

	void SortItems(std::vector<const Item*>& a_items, Sort a_sort)
	{
		// Ties fall back to the name so the order is stable and reads sensibly - a value sort over
		// three hundred items worth 0 gold is otherwise arbitrary noise.
		// Compares the key that was lower-cased once at build time. This used to lower-case both
		// names INSIDE the comparator, which is two heap allocations per comparison - about
		// 2*N*log(N) of them for every sort, on every frame the page drew.
		const auto byName = [](const Item* a, const Item* b) {
			return a->sortKeyLower < b->sortKeyLower;
		};

		switch (a_sort)
		{
		case Sort::kNameDesc:
			std::sort(a_items.begin(), a_items.end(), [&](const Item* a, const Item* b) { return byName(b, a); });
			break;
		case Sort::kValueDesc:
			std::sort(a_items.begin(), a_items.end(), [&](const Item* a, const Item* b) {
				return a->value != b->value ? a->value > b->value : byName(a, b); });
			break;
		case Sort::kValueAsc:
			std::sort(a_items.begin(), a_items.end(), [&](const Item* a, const Item* b) {
				return a->value != b->value ? a->value < b->value : byName(a, b); });
			break;
		case Sort::kWeightDesc:
			std::sort(a_items.begin(), a_items.end(), [&](const Item* a, const Item* b) {
				return a->weight != b->weight ? a->weight > b->weight : byName(a, b); });
			break;
		case Sort::kWeightAsc:
			std::sort(a_items.begin(), a_items.end(), [&](const Item* a, const Item* b) {
				return a->weight != b->weight ? a->weight < b->weight : byName(a, b); });
			break;
		case Sort::kNameAsc:
		default:
			std::sort(a_items.begin(), a_items.end(), byName);
			break;
		}
	}

	const Item* Find(const RE::TESForm* a_form)
	{
		if (!a_form) { return nullptr; }
		for (const Item& item : g_items)
		{
			if (item.form == a_form) { return &item; }
		}
		return nullptr;
	}

	namespace
	{
		// Which base objects a quest actually calls its own.
		//
		// ASKED, not inferred (rule 30): every loaded quest is walked, and every alias that the
		// game itself flags IsQuestObject() contributes the form it points at - the base object of
		// a "create reference to object" alias, or the base object of a forced reference. A base
		// form carries no quest-item flag of its own, so this is the only honest way to answer the
		// question for a catalogue of forms rather than of inventory entries.
		std::size_t MarkQuestObjects()
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler) { return 0; }

			std::unordered_set<RE::FormID> questForms;
			std::size_t aliasesSeen = 0;

			for (RE::TESQuest* quest : handler->GetFormArray<RE::TESQuest>())
			{
				if (!quest) { continue; }

				for (RE::BGSBaseAlias* alias : quest->aliases)
				{
					if (!alias) { continue; }
					++aliasesSeen;

					if (!alias->IsQuestObject()) { continue; }

					auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(alias);
					if (!refAlias) { continue; }

					switch (refAlias->fillType.get())
					{
					case RE::BGSBaseAlias::FILL_TYPE::kCreated:
						if (auto* object = refAlias->fillData.created.object)
						{
							questForms.insert(object->GetFormID());
						}
						break;

					case RE::BGSBaseAlias::FILL_TYPE::kForced:
						if (auto ref = refAlias->fillData.forced.forcedRef.get())
						{
							if (auto* base = ref->GetBaseObject())
							{
								questForms.insert(base->GetFormID());
							}
						}
						break;

					default:
						break;
					}
				}
			}

			std::size_t marked = 0;
			for (Item& item : g_items)
			{
				item.questItem = questForms.count(item.formID) != 0;
				if (item.questItem) { ++marked; }
			}

			logger::info("catalog: {} quest object(s) across {} alias(es) - those items are marked "
						 "and can be hidden from the page", marked, aliasesSeen);
			return marked;
		}
	}

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

		CollectCraftingParts();
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

		MarkQuestObjects();

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
		++g_generation;   // every cached query result built against the old catalogue is now stale
		std::size_t ench = 0;
		for (const Item& i : g_items) { if (i.enchanted) { ++ench; } }

		logger::info("catalog: {} item(s) from {} plugin(s) that provide any ({} light, {} enchanted variants)",
					 g_items.size(), g_plugins.size(), light, ench);
		return g_items.size();
	}

	std::vector<const Item*> ItemsOf(std::uint32_t a_pluginIndex, std::string_view a_search,
									 bool a_kindFilter[static_cast<std::size_t>(Kind::kCount)],
									 bool a_showEnchanted, bool a_showQuestItems, Sort a_sort)
	{
		std::vector<const Item*> out;
		const std::string needle = Lower(a_search);

		for (const Item& item : g_items)
		{
			if (item.pluginIndex != a_pluginIndex) { continue; }
			if (!a_showEnchanted && item.enchanted) { continue; }
			if (!a_showQuestItems && item.questItem) { continue; }
			if (a_kindFilter && !a_kindFilter[static_cast<std::size_t>(item.kind)]) { continue; }
			if (!needle.empty() &&
				!Contains(item.nameLower, needle) && !Contains(item.editorLower, needle))
			{
				continue;
			}
			out.push_back(&item);
		}

		SortItems(out, a_sort);
		return out;
	}

	std::vector<const Item*> SearchAll(std::string_view a_search,
									   bool a_kindFilter[static_cast<std::size_t>(Kind::kCount)],
									   bool a_showEnchanted, bool a_showQuestItems, Sort a_sort,
									   std::size_t a_limit)
	{
		std::vector<const Item*> out;
		const std::string needle = Lower(a_search);
		if (needle.empty()) { return out; }

		for (const Item& item : g_items)
		{
			if (!a_showEnchanted && item.enchanted) { continue; }
			if (!a_showQuestItems && item.questItem) { continue; }
			if (a_kindFilter && !a_kindFilter[static_cast<std::size_t>(item.kind)]) { continue; }
			if (!Contains(item.nameLower, needle) && !Contains(item.editorLower, needle)) { continue; }
			out.push_back(&item);
			if (a_limit && out.size() >= a_limit) { break; }
		}

		// Sorted AFTER the cap, deliberately. Sorting the whole match set first would be the
		// honest ordering but means ordering tens of thousands of forms on every keystroke; the
		// page already tells the reader the list was cut short.
		SortItems(out, a_sort);
		return out;
	}

	std::uint32_t Generation() { return g_generation; }

	namespace
	{
		// One remembered answer per query shape. The page asks the SAME question on every frame it
		// draws - the search box has not changed, nor the filters, nor the sort - and the honest
		// answer is the one from last frame. Recomputing it meant walking all 26,000 items and
		// sorting the survivors sixty times a second for no new information, which is where most
		// of the frame rate went while the page was open (phbd01, 2026-09-19).
		struct QueryCache
		{
			bool                      valid = false;
			std::uint32_t             generation = 0;
			std::uint32_t             pluginIndex = 0;
			std::string               search;
			bool                      kinds[static_cast<std::size_t>(Kind::kCount)]{};
			bool                      showEnchanted = false;
			bool                      showQuestItems = false;
			Sort                      sort = Sort::kNameAsc;
			std::size_t               limit = 0;
			std::vector<const Item*>  result;

			[[nodiscard]] bool Matches(std::uint32_t a_plugin, std::string_view a_search,
									   const bool* a_kinds, bool a_ench, bool a_quest,
									   Sort a_sort, std::size_t a_limit) const
			{
				if (!valid || generation != g_generation) { return false; }
				if (pluginIndex != a_plugin || search != a_search) { return false; }
				if (showEnchanted != a_ench || showQuestItems != a_quest) { return false; }
				if (sort != a_sort || limit != a_limit) { return false; }
				if (a_kinds)
				{
					for (std::size_t i = 0; i < static_cast<std::size_t>(Kind::kCount); ++i)
					{
						if (kinds[i] != a_kinds[i]) { return false; }
					}
				}
				return true;
			}

			void Remember(std::uint32_t a_plugin, std::string_view a_search, const bool* a_kinds,
						  bool a_ench, bool a_quest, Sort a_sort, std::size_t a_limit,
						  std::vector<const Item*>&& a_result)
			{
				valid = true;
				generation = g_generation;
				pluginIndex = a_plugin;
				search.assign(a_search);
				showEnchanted = a_ench;
				showQuestItems = a_quest;
				sort = a_sort;
				limit = a_limit;
				if (a_kinds)
				{
					for (std::size_t i = 0; i < static_cast<std::size_t>(Kind::kCount); ++i) { kinds[i] = a_kinds[i]; }
				}
				result = std::move(a_result);
			}
		};

		QueryCache g_perPlugin;
		QueryCache g_searchAll;
	}

	const std::vector<const Item*>& ItemsOfCached(std::uint32_t a_pluginIndex, std::string_view a_search,
												  bool a_kindFilter[static_cast<std::size_t>(Kind::kCount)],
												  bool a_showEnchanted, bool a_showQuestItems, Sort a_sort)
	{
		if (!g_perPlugin.Matches(a_pluginIndex, a_search, a_kindFilter, a_showEnchanted, a_showQuestItems, a_sort, 0))
		{
			g_perPlugin.Remember(a_pluginIndex, a_search, a_kindFilter, a_showEnchanted, a_showQuestItems, a_sort, 0,
								 ItemsOf(a_pluginIndex, a_search, a_kindFilter, a_showEnchanted, a_showQuestItems, a_sort));
		}
		return g_perPlugin.result;
	}

	const std::vector<const Item*>& SearchAllCached(std::string_view a_search,
													bool a_kindFilter[static_cast<std::size_t>(Kind::kCount)],
													bool a_showEnchanted, bool a_showQuestItems, Sort a_sort,
													std::size_t a_limit)
	{
		if (!g_searchAll.Matches(0, a_search, a_kindFilter, a_showEnchanted, a_showQuestItems, a_sort, a_limit))
		{
			g_searchAll.Remember(0, a_search, a_kindFilter, a_showEnchanted, a_showQuestItems, a_sort, a_limit,
								 SearchAll(a_search, a_kindFilter, a_showEnchanted, a_showQuestItems, a_sort, a_limit));
		}
		return g_searchAll.result;
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
