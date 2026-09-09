#include "PCH.h"

#include "UI.h"

#include "SKSEMenuFramework.h"

#include "Catalog.h"
#include "Favourites.h"
#include "Settings.h"

#include "utils/Logger.h"
#include "utils/Strings.h"
#include "utils/Toggle.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace UI
{
	namespace
	{
		constexpr std::size_t kKindCount = static_cast<std::size_t>(Catalog::Kind::kCount);

		// The kind names the PAGE shows, in the player's language. Catalog::KindName stays English
		// deliberately: that one is what the log lines and the aie.catalog DevBench tool print, and
		// a tool's identifiers must not change with the language the player happens to be reading.
		const char* KindLabel(Catalog::Kind a_kind)
		{
			switch (a_kind)
			{
			case Catalog::Kind::kWeapon:     return strings::TR("AIE_KindWeapon",     "Weapon");
			case Catalog::Kind::kArmor:      return strings::TR("AIE_KindArmor",      "Armor");
			case Catalog::Kind::kAmmo:       return strings::TR("AIE_KindAmmo",       "Ammo");
			case Catalog::Kind::kBook:       return strings::TR("AIE_KindBook",       "Book");
			case Catalog::Kind::kIngredient: return strings::TR("AIE_KindIngredient", "Ingredient");
			case Catalog::Kind::kPotion:     return strings::TR("AIE_KindPotion",     "Potion");
			case Catalog::Kind::kScroll:     return strings::TR("AIE_KindScroll",     "Scroll");
			case Catalog::Kind::kSoulGem:    return strings::TR("AIE_KindSoulGem",    "Soul gem");
			case Catalog::Kind::kKey:        return strings::TR("AIE_KindKey",        "Key");
			case Catalog::Kind::kMisc:       return strings::TR("AIE_KindMisc",       "Misc");
			case Catalog::Kind::kLight:      return strings::TR("AIE_KindLight",      "Light");
			case Catalog::Kind::kSpell:      return strings::TR("AIE_KindSpell",      "Spell");
			default:                         return Catalog::KindName(a_kind);
			}
		}

		char        g_pluginFilter[128] = {};
		char        g_itemSearch[128] = {};
		int         g_selectedPlugin = -1;
		int         g_addCount = 1;
		bool        g_searchEverywhere = false;
		// Skyrim ships hundreds of enchanted variants of every base weapon and armour piece,
		// and they bury what a plugin actually adds. Off by default for that reason.
		bool        g_showEnchanted = false;
		bool        g_kind[kKindCount];
		bool        g_kindInit = false;
		std::string g_status;

	}


	namespace
	{

		// A broad search across a big load order can match tens of thousands of forms. The page
		// shows the first slice and says so, rather than trying to draw all of them.
		constexpr std::size_t kSearchLimit = 500;

		void InitKinds()
		{
			if (g_kindInit) { return; }
			for (bool& b : g_kind) { b = true; }
			g_kindInit = true;
		}

		// Every ImGui call this page makes, as the symbol the framework must export. The wrappers
		// in SKSEMenuFramework.h resolve these with GetProcAddress and then CALL THE RESULT WITHOUT
		// CHECKING IT, so a symbol the loaded framework does not have is a jump to address zero.
		// That is not theoretical: Apocrypha Menu Framework 1.4.9 exports 50 functions and has none
		// of the table API, no igSmallButton and no igInputTextWithHint, and calling them crashed the
		// game the moment this page drew. So the page is built from the calls 1.4.9 already has, and
		// this check refuses to register at all if even one of them is missing.
		constexpr const char* kRequired[] = {
			"AddSectionItem",
			"igTextV", "igTextDisabledV", "igTextWrappedV",
			"igButton", "igCheckbox", "igInputInt", "igInputText",
			"igSelectable_Bool", "igBeginChild_Str", "igEndChild",
			"igSeparator", "igSeparatorText", "igSpacing", "igSameLine",
			"igPushItemWidth", "igPopItemWidth", "igPushID_Str", "igPopID",
			// the hand-drawn Toggle (rule 32 - a boolean is a switch, never a tick-box)
			"igGetCursorScreenPos", "igGetWindowDrawList", "igGetFrameHeight",
			"igInvisibleButton", "igIsItemHovered",
			"ImDrawList_AddRectFilled", "ImDrawList_AddCircleFilled",
			"igCombo_Str_arr"
		};

		bool HasRequiredExports()
		{
			for (const char* name : kRequired)
			{
				// Resolved exactly the way the header's own wrappers resolve it, so this cannot
				// disagree with what they will actually call.
				if (!GetMenuFrameworkFunction<void*>(name))
				{
					logger::warn("the loaded menu framework does not export \"{}\"; the page will not "
								 "be registered rather than risk calling a null pointer", name);
					return false;
				}
			}
			return true;
		}

		void DrawKindFilters()
		{
			ImGuiMCP::SeparatorText(strings::TR("AIE_Kinds", "Kinds"));
			for (std::size_t i = 0; i < kKindCount; ++i)
			{
				const auto kind = static_cast<Catalog::Kind>(i);
				if (kind == Catalog::Kind::kSpell && !settings::general::includeSpells) { continue; }

				ImGuiMCP::Toggle(KindLabel(kind), &g_kind[i]);
				if ((i % 4) != 3 && i + 1 < kKindCount) { ImGuiMCP::SameLine(); }
			}
			ImGuiMCP::Spacing();

			// Sorting. The catalogue's own order is the order forms sit in the game's arrays, which
			// means nothing to a reader, so this is the first thing most people will want.
			{
				int sort = static_cast<int>(settings::general::sortMode);
				std::vector<std::string> labelStore;
				std::vector<const char*> labels;
				// Translated here rather than in Catalog, which has no business knowing about the
				// page's language. Catalog::SortName stays the English identifier the DevBench tool
				// and the log use.
				static constexpr const char* kSortKeys[] = {
					"AIE_Sort_NameAsc", "AIE_Sort_NameDesc", "AIE_Sort_ValueDesc",
					"AIE_Sort_ValueAsc", "AIE_Sort_WeightDesc", "AIE_Sort_WeightAsc"
				};
				labelStore.reserve(static_cast<std::size_t>(Catalog::Sort::kCount));
				for (std::size_t i = 0; i < static_cast<std::size_t>(Catalog::Sort::kCount); ++i)
				{
					labelStore.emplace_back(strings::TR(kSortKeys[i],
										   Catalog::SortName(static_cast<Catalog::Sort>(i))));
				}
				for (const auto& l : labelStore) { labels.push_back(l.c_str()); }

				if (ImGuiMCP::Combo(strings::TR("AIE_SortBy", "Sort by"), &sort, labels.data(),
									static_cast<int>(labels.size())))
				{
					settings::general::sortMode = static_cast<std::uint32_t>(sort);
				}
			}

			ImGuiMCP::Toggle(strings::TR("AIE_ShowQuestItems", "Show quest items"),
							 &settings::general::showQuestItems);
			ImGuiMCP::SameLine();
			ImGuiMCP::TextDisabled("%s", strings::TR("AIE_QuestHint",
								   "items a quest calls its own - always tagged [quest] when shown"));



			ImGuiMCP::Spacing();
			if (ImGuiMCP::Button(strings::TR("AIE_All", "All")))
			{
				for (bool& b : g_kind) { b = true; }
			}
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button(strings::TR("AIE_None", "None")))
			{
				for (bool& b : g_kind) { b = false; }
			}
		}

		// One item as a plain row: the Add button, the name, then the quiet details. No table API -
		// older frameworks do not export it, and a row reads just as well.
		void DrawItemRow(const Catalog::Item& a_item, bool a_showPlugin)
		{
			// The form ID makes every row's widgets unique; without it ImGui merges buttons that
			// share a label and clicking one adds a different item.
			const std::string id = std::to_string(a_item.formID);
			ImGuiMCP::PushID(id.c_str());

			if (ImGuiMCP::Button(strings::TR("AIE_Add", "Add")))
			{
				const auto count = static_cast<std::uint32_t>(g_addCount < 1 ? 1 : g_addCount);
				Catalog::GiveToPlayer(a_item.form, count);
				g_status = std::string(strings::TR("AIE_Added", "Added")) + " " +
						   std::to_string(count) + " x " +
						   (a_item.name.empty() ? a_item.editorID : a_item.name);
			}

			// The favourite toggle. A filled star means it is on the Favourites page; the label is
			// the whole control, so there is nothing to learn.
			ImGuiMCP::SameLine();
			const bool fav = favourites::Contains(a_item.form);
			if (ImGuiMCP::Button(fav ? strings::TR("AIE_FavOn", "[*]") : strings::TR("AIE_FavOff", "[ ]")))
			{
				favourites::Toggle(a_item.form, a_item.name.empty() ? a_item.editorID : a_item.name);
			}
			if (ImGuiMCP::IsItemHovered())
			{
				ImGuiMCP::SetTooltip("%s", fav ? strings::TR("AIE_FavRemoveTip", "Remove from favourites")
											   : strings::TR("AIE_FavAddTip", "Add to favourites"));
			}

			ImGuiMCP::SameLine();
			ImGuiMCP::Text("%s", a_item.name.empty() ? a_item.editorID.c_str() : a_item.name.c_str());


			// A quest item is called out wherever it appears, whether or not they are being hidden -
			// handing yourself one can confuse the quest that owns it, and that is worth knowing
			// before you press Add rather than afterwards.
			if (a_item.questItem)
			{
				ImGuiMCP::SameLine();
				ImGuiMCP::TextDisabled("%s", strings::TR("AIE_QuestTag", "[quest]"));
			}

			ImGuiMCP::SameLine();
			if (a_showPlugin)
			{
				const auto& plugins = Catalog::Plugins();
				ImGuiMCP::TextDisabled("- %s, %s, 0x%08X", KindLabel(a_item.kind),
									   a_item.pluginIndex < plugins.size()
										   ? plugins[a_item.pluginIndex].fileName.c_str() : "?",
									   a_item.formID);
			}
			else
			{
				ImGuiMCP::TextDisabled("- %s, 0x%08X", KindLabel(a_item.kind), a_item.formID);
			}

			ImGuiMCP::PopID();
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled())
		{
			logger::info("No menu framework is installed; the explorer page will not be shown "
						 "(the DevBench tool still works)");
			return;
		}
		if (!HasRequiredExports())
		{
			logger::warn("The installed menu framework is older than this page needs. Update it "
						 "(Apocrypha Menu Framework, or SKSE Menu Framework version 3 or newer).");
			return;
		}

		SKSEMenuFramework::SetSection("Item Explorer");
		SKSEMenuFramework::AddSectionItem("Browse", ExplorerPanel::Render);
		SKSEMenuFramework::AddSectionItem("Favourites", FavouritesPanel::Render);
		logger::info("Registered the explorer page with the menu framework");
	}

	void __stdcall ExplorerPanel::Render()
	{
		strings::Tick();
		InitKinds();

		if (!Catalog::Built())
		{
			ImGuiMCP::TextWrapped("%s", strings::TR("AIE_NotBuilt",
				"The catalogue has not been read yet. It lists every plugin your game loaded and "
				"every item each one adds, and it is read once and kept."));
			ImGuiMCP::Spacing();
			if (ImGuiMCP::Button(strings::TR("AIE_Build", "Read the load order")))
			{
				const std::size_t n = Catalog::Build();
				g_status = std::to_string(n) + " " +
						   strings::TR("AIE_ItemsFound", "items found");
			}
			if (!g_status.empty())
			{
				ImGuiMCP::Spacing();
				ImGuiMCP::TextDisabled("%s", g_status.c_str());
			}
			return;
		}

		const auto& plugins = Catalog::Plugins();

		ImGuiMCP::Text("%s: %d %s, %d %s", strings::TR("AIE_Loaded", "Loaded"),
					   static_cast<int>(plugins.size()), strings::TR("AIE_Plugins", "plugins"),
					   static_cast<int>(Catalog::Items().size()), strings::TR("AIE_Items", "items"));
		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button(strings::TR("AIE_Rebuild", "Re-read")))
		{
			Catalog::Build();
			g_selectedPlugin = -1;
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::PushItemWidth(220.0F);
		ImGuiMCP::InputInt(strings::TR("AIE_HowMany", "How many"), &g_addCount);
		ImGuiMCP::PopItemWidth();
		if (g_addCount < 1) { g_addCount = 1; }
		ImGuiMCP::SameLine();
		ImGuiMCP::Toggle(strings::TR("AIE_SearchEverywhere", "Search every plugin"), &g_searchEverywhere);
		ImGuiMCP::Toggle(strings::TR("AIE_ShowEnchanted", "Show enchanted variants"), &g_showEnchanted);
		ImGuiMCP::SameLine();
		ImGuiMCP::TextDisabled("%s", strings::TR("AIE_EnchantedHint",
							   "off hides every \"of Cold\" style variant, leaving the base equipment"));

		DrawKindFilters();
		ImGuiMCP::Spacing();
		ImGuiMCP::Separator();

		// ---- Search everywhere: one list, no plugin picking ----
		if (g_searchEverywhere)
		{
			ImGuiMCP::PushItemWidth(420.0F);
			ImGuiMCP::Text("%s", strings::TR("AIE_SearchHint", "Search every plugin by name or editor ID"));
			ImGuiMCP::InputText("##search_all", g_itemSearch, sizeof(g_itemSearch));
			ImGuiMCP::PopItemWidth();

			const auto hits = Catalog::SearchAll(g_itemSearch, g_kind, g_showEnchanted,
												 settings::general::showQuestItems,
												 static_cast<Catalog::Sort>(settings::general::sortMode),
												 kSearchLimit);
			if (g_itemSearch[0] == '\0')
			{
				ImGuiMCP::TextDisabled("%s", strings::TR("AIE_TypeToSearch", "Type to search."));
				return;
			}

			ImGuiMCP::TextDisabled("%d %s%s", static_cast<int>(hits.size()),
								   strings::TR("AIE_Matches", "matches"),
								   hits.size() >= kSearchLimit
									   ? strings::TR("AIE_Capped", " (showing the first 500)") : "");

			if (ImGuiMCP::BeginChild("aie_all", ImGuiMCP::ImVec2(0.0F, 460.0F), 1))
			{
				for (const auto* item : hits) { DrawItemRow(*item, true); }
			}
			ImGuiMCP::EndChild();

			if (!g_status.empty())
			{
				ImGuiMCP::Spacing();
				ImGuiMCP::TextDisabled("%s", g_status.c_str());
			}
			return;
		}

		// ---- Plugin, then its items ----
		ImGuiMCP::PushItemWidth(300.0F);
		ImGuiMCP::Text("%s", strings::TR("AIE_FilterPlugins", "Filter plugins"));
		ImGuiMCP::SameLine();
		ImGuiMCP::InputText("##plugin_filter", g_pluginFilter, sizeof(g_pluginFilter));
		ImGuiMCP::PopItemWidth();
		ImGuiMCP::SameLine();
		ImGuiMCP::PushItemWidth(300.0F);
		ImGuiMCP::Text("%s", strings::TR("AIE_FilterItems", "Filter items"));
		ImGuiMCP::SameLine();
		ImGuiMCP::InputText("##item_search", g_itemSearch, sizeof(g_itemSearch));
		ImGuiMCP::PopItemWidth();
		ImGuiMCP::Spacing();

		std::string filterLower(g_pluginFilter);
		std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
					   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		if (ImGuiMCP::BeginChild("aie_plugins", ImGuiMCP::ImVec2(320.0F, 420.0F), 1))
		{
			for (std::size_t i = 0; i < plugins.size(); ++i)
			{
				const auto& p = plugins[i];
				if (!filterLower.empty())
				{
					std::string nameLower = p.fileName;
					std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
								   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (nameLower.find(filterLower) == std::string::npos) { continue; }
				}
				if (p.itemCount == 0) { continue; }

				const std::string label = p.fileName + "  (" + std::to_string(p.itemCount) + ")" +
										  (p.light ? "  [ESL]" : "") + "##p" + std::to_string(i);
				if (ImGuiMCP::Selectable(label.c_str(), g_selectedPlugin == static_cast<int>(i)))
				{
					g_selectedPlugin = static_cast<int>(i);
				}
			}
		}
		ImGuiMCP::EndChild();

		ImGuiMCP::SameLine();

		if (ImGuiMCP::BeginChild("aie_items", ImGuiMCP::ImVec2(0.0F, 420.0F), 1))
		{
			if (g_selectedPlugin < 0 || g_selectedPlugin >= static_cast<int>(plugins.size()))
			{
				ImGuiMCP::TextDisabled("%s", strings::TR("AIE_PickPlugin",
										"Pick a plugin on the left to see what it adds."));
			}
			else
			{
				const auto items = Catalog::ItemsOf(static_cast<std::uint32_t>(g_selectedPlugin),
													g_itemSearch, g_kind, g_showEnchanted,
													settings::general::showQuestItems,
													static_cast<Catalog::Sort>(settings::general::sortMode));
				ImGuiMCP::TextDisabled("%s - %d %s", plugins[g_selectedPlugin].fileName.c_str(),
									   static_cast<int>(items.size()), strings::TR("AIE_Shown", "shown"));

				for (const auto* item : items) { DrawItemRow(*item, false); }
			}
		}
		ImGuiMCP::EndChild();

		if (!g_status.empty())
		{
			ImGuiMCP::Spacing();
			ImGuiMCP::TextDisabled("%s", g_status.c_str());
		}

		// The 3D preview follows whatever row was last clicked. Both calls belong at the end of the
		// frame's drawing: Show only records the wish, Tick is what actually talks to the game's
		// inventory renderer, and it has to happen on the frame it is drawn.
	}

	// The favourites page. Deliberately a SECOND page rather than a filter on the first: the whole
	// point is a short list you keep coming back to, and hiding it behind the same search box the
	// catalogue uses would defeat that.
	void __stdcall FavouritesPanel::Render()
	{
		strings::Tick();

		ImGuiMCP::Text("%s", strings::TR("AIE_FavTitle", "Favourites"));
		ImGuiMCP::Separator();

		const auto& all = favourites::All();

		if (all.empty())
		{
			ImGuiMCP::TextWrapped("%s", strings::TR("AIE_FavEmpty",
								  "Nothing here yet. Find something on the Browse page and press the "
								  "star beside it. Favourites are kept between sessions, and they are "
								  "stored per plugin, so they survive changes to your load order."));
			return;
		}

		if (!Catalog::Built())
		{
			ImGuiMCP::TextWrapped("%s", strings::TR("AIE_FavNeedsCatalogue",
								  "Read the load order on the Browse page first - these are stored as "
								  "plugin names and need the catalogue to turn back into items."));
			return;
		}

		ImGuiMCP::TextDisabled("%s: %d", strings::TR("AIE_FavCount", "Saved"), static_cast<int>(all.size()));

		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button(strings::TR("AIE_FavClear", "Clear all")))
		{
			favourites::Clear();
			return;
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Separator();

		// Built from the saved list, then ordered by the SAME sort the Browse page is using, so
		// switching pages does not reshuffle everything under the reader.
		std::vector<const Catalog::Item*> items;
		items.reserve(all.size());
		for (const auto& e : all)
		{
			if (const Catalog::Item* item = Catalog::Find(e.form)) { items.push_back(item); }
		}
		Catalog::SortItems(items, static_cast<Catalog::Sort>(settings::general::sortMode));

		if (items.empty())
		{
			ImGuiMCP::TextWrapped("%s", strings::TR("AIE_FavNoneResolved",
								  "None of the saved favourites are in the current load order."));
			return;
		}

		if (ImGuiMCP::BeginChild("aie_favs", ImGuiMCP::ImVec2(0.0F, 460.0F), 1))
		{
			// Shown WITH the plugin name: a favourites list is the one place you are most likely to
			// be looking at two similarly named things from different mods.
			for (const auto* item : items) { DrawItemRow(*item, true); }
		}
		ImGuiMCP::EndChild();

        if (!g_status.empty())
        {
            ImGuiMCP::Spacing();
            ImGuiMCP::TextDisabled("%s", g_status.c_str());
        }

	}
}
