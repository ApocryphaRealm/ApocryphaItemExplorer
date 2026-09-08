#include "PCH.h"

#include "UI.h"

#include "SKSEMenuFramework.h"

#include "Catalog.h"
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

		char        g_pluginFilter[128] = {};
		char        g_itemSearch[128] = {};
		int         g_selectedPlugin = -1;
		int         g_addCount = 1;
		bool        g_searchEverywhere = false;
		bool        g_kind[kKindCount];
		bool        g_kindInit = false;
		std::string g_status;

		// A broad search across a big load order can match tens of thousands of forms. The page
		// shows the first slice and says so, rather than trying to draw all of them.
		constexpr std::size_t kSearchLimit = 500;

		void InitKinds()
		{
			if (g_kindInit) { return; }
			for (bool& b : g_kind) { b = true; }
			g_kindInit = true;
		}

		bool HasRequiredExports()
		{
			constexpr const char* required[] = {
				"AddSectionItem", "igTextV", "igTextWrappedV", "igSeparatorText",
				"igButton", "igSmallButton", "igSameLine", "igSpacing", "igSeparator",
				"igInputTextWithHint", "igInputInt", "igCheckbox", "igSelectable_Bool",
				"igBeginChild_Str", "igEndChild", "igBeginTable", "igEndTable",
				"igTableNextRow", "igTableNextColumn", "igTableSetupColumn", "igTableHeadersRow",
				"igPushItemWidth", "igPopItemWidth", "igTextDisabledV"
			};
			const HMODULE mod = GetModuleHandleA("SKSEMenuFramework.dll");
			if (!mod) { return true; }  // the framework answers through its own alias; assume current
			for (const char* name : required)
			{
				if (!GetProcAddress(mod, name))
				{
					logger::warn("menu framework is missing \"{}\"", name);
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

				ImGuiMCP::Checkbox(Catalog::KindName(kind), &g_kind[i]);
				if ((i % 4) != 3 && i + 1 < kKindCount) { ImGuiMCP::SameLine(); }
			}
			ImGuiMCP::Spacing();
			if (ImGuiMCP::SmallButton(strings::TR("AIE_All", "All")))
			{
				for (bool& b : g_kind) { b = true; }
			}
			ImGuiMCP::SameLine();
			if (ImGuiMCP::SmallButton(strings::TR("AIE_None", "None")))
			{
				for (bool& b : g_kind) { b = false; }
			}
		}

		void DrawItemRow(const Catalog::Item& a_item, bool a_showPlugin)
		{
			ImGuiMCP::TableNextRow();

			ImGuiMCP::TableNextColumn();
			ImGuiMCP::Text("%s", a_item.name.empty() ? a_item.editorID.c_str() : a_item.name.c_str());

			ImGuiMCP::TableNextColumn();
			ImGuiMCP::TextDisabled("%s", Catalog::KindName(a_item.kind));

			if (a_showPlugin)
			{
				ImGuiMCP::TableNextColumn();
				const auto& plugins = Catalog::Plugins();
				ImGuiMCP::TextDisabled("%s", a_item.pluginIndex < plugins.size()
											 ? plugins[a_item.pluginIndex].fileName.c_str() : "?");
			}

			ImGuiMCP::TableNextColumn();
			ImGuiMCP::TextDisabled("0x%08X", a_item.formID);

			ImGuiMCP::TableNextColumn();
			// The button label has to be unique per row or ImGui merges them, so the form ID goes
			// into an id suffix rather than into the visible text.
			const std::string label = std::string(strings::TR("AIE_Add", "Add")) +
									  "##" + std::to_string(a_item.formID);
			if (ImGuiMCP::SmallButton(label.c_str()))
			{
				const auto count = static_cast<std::uint32_t>(std::max(1, g_addCount));
				Catalog::GiveToPlayer(a_item.form, count);
				g_status = std::string(strings::TR("AIE_Added", "Added")) + " " +
						   std::to_string(count) + " x " +
						   (a_item.name.empty() ? a_item.editorID : a_item.name);
			}
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
		if (ImGuiMCP::SmallButton(strings::TR("AIE_Rebuild", "Re-read")))
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
		ImGuiMCP::Checkbox(strings::TR("AIE_SearchEverywhere", "Search every plugin"), &g_searchEverywhere);

		DrawKindFilters();
		ImGuiMCP::Spacing();
		ImGuiMCP::Separator();

		// ---- Search everywhere: one list, no plugin picking ----
		if (g_searchEverywhere)
		{
			ImGuiMCP::PushItemWidth(420.0F);
			ImGuiMCP::InputTextWithHint("##search_all", strings::TR("AIE_SearchHint",
										"Search every plugin by name or editor ID"),
										g_itemSearch, sizeof(g_itemSearch));
			ImGuiMCP::PopItemWidth();

			const auto hits = Catalog::SearchAll(g_itemSearch, g_kind, kSearchLimit);
			if (g_itemSearch[0] == '\0')
			{
				ImGuiMCP::TextDisabled("%s", strings::TR("AIE_TypeToSearch", "Type to search."));
				return;
			}

			ImGuiMCP::TextDisabled("%d %s%s", static_cast<int>(hits.size()),
								   strings::TR("AIE_Matches", "matches"),
								   hits.size() >= kSearchLimit
									   ? strings::TR("AIE_Capped", " (showing the first 500)") : "");

			if (ImGuiMCP::BeginTable("aie_all", 5, 0))
			{
				ImGuiMCP::TableSetupColumn(strings::TR("AIE_Name", "Name"));
				ImGuiMCP::TableSetupColumn(strings::TR("AIE_Kind", "Kind"));
				ImGuiMCP::TableSetupColumn(strings::TR("AIE_Plugin", "Plugin"));
				ImGuiMCP::TableSetupColumn(strings::TR("AIE_FormID", "Form ID"));
				ImGuiMCP::TableSetupColumn("");
				ImGuiMCP::TableHeadersRow();
				for (const auto* item : hits) { DrawItemRow(*item, true); }
				ImGuiMCP::EndTable();
			}

			if (!g_status.empty())
			{
				ImGuiMCP::Spacing();
				ImGuiMCP::TextDisabled("%s", g_status.c_str());
			}
			return;
		}

		// ---- Plugin, then its items ----
		ImGuiMCP::PushItemWidth(300.0F);
		ImGuiMCP::InputTextWithHint("##plugin_filter", strings::TR("AIE_FilterPlugins", "Filter plugins"),
									g_pluginFilter, sizeof(g_pluginFilter));
		ImGuiMCP::PopItemWidth();
		ImGuiMCP::SameLine();
		ImGuiMCP::PushItemWidth(300.0F);
		ImGuiMCP::InputTextWithHint("##item_search", strings::TR("AIE_FilterItems", "Filter items"),
									g_itemSearch, sizeof(g_itemSearch));
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
													g_itemSearch, g_kind);
				ImGuiMCP::TextDisabled("%s - %d %s", plugins[g_selectedPlugin].fileName.c_str(),
									   static_cast<int>(items.size()), strings::TR("AIE_Shown", "shown"));

				if (ImGuiMCP::BeginTable("aie_one", 4, 0))
				{
					ImGuiMCP::TableSetupColumn(strings::TR("AIE_Name", "Name"));
					ImGuiMCP::TableSetupColumn(strings::TR("AIE_Kind", "Kind"));
					ImGuiMCP::TableSetupColumn(strings::TR("AIE_FormID", "Form ID"));
					ImGuiMCP::TableSetupColumn("");
					ImGuiMCP::TableHeadersRow();
					for (const auto* item : items) { DrawItemRow(*item, false); }
					ImGuiMCP::EndTable();
				}
			}
		}
		ImGuiMCP::EndChild();

		if (!g_status.empty())
		{
			ImGuiMCP::Spacing();
			ImGuiMCP::TextDisabled("%s", g_status.c_str());
		}
	}
}
