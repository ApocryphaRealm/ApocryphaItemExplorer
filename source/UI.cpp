#include "PCH.h"

#include "UI.h"

#include "SKSEMenuFramework.h"

#include "Catalog.h"
#include "PreciseSlider.h"
#include "Favourites.h"
#include "ItemInfo.h"
#include "Preview.h"
#include "Settings.h"

#include "utils/Logger.h"
#include "utils/Strings.h"
#include "utils/Toggle.h"

#include <unordered_map>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
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
		// The selected plugin by NAME, so a rebuild (the "Show items with no name" switch, or the DevBench tool's
		// op=build / op=set) that adds or drops plugins keeps the same plugin selected rather than whatever now
		// sits at the old index.
		std::string g_selectedPluginName;
		// Set by the "Show items with no name" switch; the page re-reads the catalogue once the filters are drawn.
		bool        g_unnamedChanged = false;
		// How many of each pile to add, one amount per item (2026-10-02: a slider on every row you hold a pile of, on
		// Browse AND Favourites, instead of one "How many" at the top of Browse). Kept for the session.
		std::unordered_map<RE::FormID, int> g_rowCount;
		// Gold gets its own amount because its sane range is nothing like anything else's: you ask
		// for 5 potions and 5000 septims. One slider covering both would have useless precision at
		// the low end, so the special case lives on the one row it applies to (the owner, 2026-09-10:
		// "up to 50 for consumables and 10000 for gold").
		int         g_goldCount = 100;

		// The row the 3D preview follows: whatever the cursor is over, or what the D-pad has
		// focused. A pointer into the catalogue, which is stable until the catalogue is rebuilt -
		// both rebuild sites below clear it.
		const Catalog::Item* g_previewItem = nullptr;

		// After circle gives the sticks back, the highlight has to land on the row the player was
		// handling - not on the list's own box (the owner, 2026-09-19: "the nav box will default to
		// the nav box of the listed items not a specific item that you were selecting beforehand").
		// Set when handling ends; consumed by the first row that matches, one frame later.
		const Catalog::Item* g_refocusItem = nullptr;

		// Set while the rows are drawn, resolved once afterwards: the mouse wins when it is over a
		// row, the keyboard/controller highlight wins when it is not.
		const Catalog::Item* g_hoverItem = nullptr;
		const Catalog::Item* g_focusItem = nullptr;

		// THE LEFT-PANE PREVIEW (1.2.0, the owner, 2026-10-23). A switch on Browse and Favourites, "Item preview in the
		// left pane": on, a card covers the framework's side list - a square 3D preview at the top, the item's description
		// and inventory details below - and it FOLLOWS the highlight: moving down the list updates it once the highlight
		// has rested on a row for kFollowRest, so holding the D-pad does not start a model load for every row it passes
		// (the Witcher 3 Item Explorer's kFollowRest). Nothing to press: A keeps its own action and fixes nothing (the
		// 1.1.1 click-to-fix is gone). The switch starts at bPreviewInLeftPane each time the menu opens and is never saved.
		bool                 g_leftPane = true;
		std::atomic<int>     g_toolLeftPane{ -1 };      // DevBench op=leftpane:<0|1>, applied on the page's next frame
		constexpr double     kFollowRest = 0.15;
		const Catalog::Item* g_candidate = nullptr;      // the row the highlight (or the moving mouse) is on now
		double               g_candidateSince = 0.0;
		const Catalog::Item* g_lastFocus = nullptr;      // last frame's highlighted row, to see the highlight move
		// CONTROLLER FOCUS BEATS A RESTING MOUSE: whichever moved last decides. A mouse that has not moved since the
		// D-pad did is only resting where the player left it, and must not pull the preview back to the row under it.
		bool                 g_mouseLeads = false;
		ImGuiMCP::ImGuiWindow* g_pageWindow = nullptr;   // the page's window, to hand the focus back to from the card
		// for the DevBench tool: where the card, the side list and the square were last frame
		std::atomic<float>   g_cardRect[4]{}, g_sideRect[4]{}, g_squareRect[4]{};
		std::atomic<bool>    g_cardDrawn{ false };

		// The Favourites page's save confirmation.
		bool  g_favSaved = false;
		float g_favSavedAt = 0.0F;
		int   g_favSavedShow = 0;


		constexpr int kStackMax = 50;      // consumables AND crafting materials
		constexpr int kGoldMax  = 10000;

		// Gold001. Matched by form id because gold carries no keyword of its own that separates it
		// from ordinary clutter - it is VendorItemClutter, same as a tin cup.
		constexpr RE::FormID kGoldFormID = 0x0000000F;

		// "Consumable" - things you hold a pile of - is decided ONCE when the catalogue is built
		// (Catalog::Item::bulk). It used to be worked out here, per row, per frame: for a crafting
		// material that means walking the form's keyword list comparing four strings, on every
		// visible row, sixty times a second.

		[[nodiscard]] bool IsGold(const Catalog::Item& a_item)
		{
			return a_item.formID == kGoldFormID;
		}
		// One list across every plugin by default (the owner, 2026-10-23), instead of making the player pick a plugin first.
		bool        g_searchEverywhere = true;
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
			"igTextV", "igTextDisabledV", "igTextWrappedV", "igPushTextWrapPos", "igPopTextWrapPos",
			"igButton", "igCheckbox", "igInputInt", "igInputText",
			"igSelectable_Bool", "igBeginChild_Str", "igEndChild",
			"igSeparator", "igSeparatorText", "igSpacing", "igSameLine",
			"igPushItemWidth", "igPopItemWidth", "igPushID_Str", "igPopID",
			// the hand-drawn Toggle (rule 32 - a boolean is a switch, never a tick-box)
			"igGetCursorScreenPos", "igGetWindowDrawList", "igGetFrameHeight",
			// 1.1.1: the name is a Selectable that spans the row, placed by hand
			"igGetContentRegionAvail", "igGetCursorPosX",
			"igInvisibleButton", "igIsItemHovered",
			"ImDrawList_AddRectFilled", "ImDrawList_AddCircleFilled",
			// the 3D preview's picture, line frame and the input it reads (1.2.0: drawn in the left-pane card's window,
			// whose own extra calls are checked separately in kLeftPaneExports)
			"igGetIO",
			"ImDrawList_AddLine", "ImDrawList_AddText_Vec2",
			"igSliderFloat", "igSliderInt", "igIsKeyDown_Nil", "igIsMouseDown_Nil", "igCombo_Str_arr",
			// 1.0.7: the 3D preview pane is an image of the engine's own render, and the row under
			// the cursor OR under D-pad focus is what it shows.
			"igIsItemFocused", "ImDrawList_AddImage", "igCalcTextSize",
			// 1.1.1: R3 takes hold of the previewed item and circle lets go.
			"igIsKeyPressed_Bool", "igIsAnyItemActive",
			"igSetKeyboardFocusHere",
			"igBeginTable", "igEndTable", "igTableNextColumn"
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
			// Three neat columns (the owner, 2026-09-18: "i want the filter toggles to be in neat columns of 3
			// toggles instead of them all being aligned to the left"). A table, so the columns line up whatever
			// each label's width is; the old SameLine chain put four ragged toggles on a row, and skipping the
			// spell toggle threw its count off.
			if (ImGuiMCP::BeginTable("##kinds", 3, ImGuiMCP::ImGuiTableFlags_SizingStretchSame))
			{
				for (std::size_t i = 0; i < kKindCount; ++i)
				{
					const auto kind = static_cast<Catalog::Kind>(i);
					if (kind == Catalog::Kind::kSpell && !settings::general::includeSpells) { continue; }
					ImGuiMCP::TableNextColumn();
					ImGuiMCP::Toggle(KindLabel(kind), &g_kind[i]);
				}
				ImGuiMCP::EndTable();
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
					settings::Save();   // rule 16: a page change persists (nothing saved it before 1.1.4)
				}
			}

			if (ImGuiMCP::Toggle(strings::TR("AIE_ShowQuestItems", "Show quest items"),
								 &settings::general::showQuestItems))
			{
				settings::Save();
			}
			ImGuiMCP::SameLine();
			ImGuiMCP::TextDisabled("%s", strings::TR("AIE_QuestHint",
								   "items a quest calls its own - always tagged [quest] when shown"));

			// Items with no in-game name (binggo123, 2026-09-29: EldenSkyrim.esp's weapons were not listed). Saved to
			// the INI at once, and the catalogue is re-read with them in or out - see ExplorerPanel::Render.
			if (ImGuiMCP::Toggle(strings::TR("AIE_ShowNoName", "Show items with no name"),
								 &settings::general::showUnnamed))
			{
				g_unnamedChanged = true;
			}
			ImGuiMCP::SameLine();
			// wrapped to the page: at 1024x768 an unwrapped hint ran under the preview box (2026-10-03 capture)
			ImGuiMCP::PushTextWrapPos(0.0F);
			ImGuiMCP::TextDisabled("%s", strings::TR("AIE_NoNameHint",
								   "items with only an editor ID, read from the plugin file - tagged (no name)"));
			ImGuiMCP::PopTextWrapPos();

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

		// Every pointer this page keeps into the catalogue dies with a rebuild - and a rebuild can come from the page
		// itself or from the DevBench tool's thread. Called at the top of each page's render and right after the page
		// rebuilds, so no row, preview or highlight ever points into a catalogue that is gone.
		void SyncWithCatalogue()
		{
			static std::uint32_t s_generation = 0;
			const std::uint32_t now = Catalog::Generation();
			if (now == s_generation) { return; }
			s_generation = now;

			g_previewItem = nullptr;
			g_candidate = nullptr;
			g_lastFocus = nullptr;
			g_refocusItem = nullptr;
			g_hoverItem = nullptr;
			g_focusItem = nullptr;

			g_selectedPlugin = -1;
			if (!g_selectedPluginName.empty())
			{
				const auto& plugins = Catalog::Plugins();
				for (std::size_t i = 0; i < plugins.size(); ++i)
				{
					if (plugins[i].fileName == g_selectedPluginName) { g_selectedPlugin = static_cast<int>(i); break; }
				}
			}
		}

		// One item as a plain row: the Add button, the name, then the quiet details. No table API -
		// older frameworks do not export it, and a row reads just as well.
		// The last widget drawn is the one under the cursor or under D-pad focus: make its row the
		// previewed one. Called after each of a row's controls, so any of them counts.
		void NoteRow(const Catalog::Item& a_item)
		{
			// The row the player was handling takes the highlight back, on the frame after circle.
			if (g_refocusItem == &a_item)
			{
				ImGuiMCP::SetKeyboardFocusHere(-1);
				g_refocusItem = nullptr;
			}
			// ONE WINNER PER FRAME (2026-09-19): the rows only RECORD what is hovered and what has the highlight; which of
			// the two drives the preview is decided once, after the list (ResolvePreview), so the request never changes
			// several times within a frame - that unloads and reloads the engine's model and no capture ever lands.
			if (ImGuiMCP::IsItemHovered()) { g_hoverItem = &a_item; }
			if (ImGuiMCP::IsItemFocused() && g_focusItem == nullptr) { g_focusItem = &a_item; }

			// R3 ON THE HIGHLIGHTED ROW hands the preview box the sticks (the owner, 2026-09-19).
			// Asked of the row that has the highlight, so the item being handled is the one the
			// player was looking at - and while the box has the sticks the highlight cannot move,
			// so it is still on this row when circle gives them back.
			if (ImGuiMCP::IsItemFocused() && !preview::Handling() &&
				ImGuiMCP::IsKeyPressed(ImGuiMCP::ImGuiKey_GamepadR3, false))
			{
				g_previewItem = &a_item;
				g_candidate = &a_item;
				preview::BeginHandling();
			}
		}

		// What the left-pane card needs from the framework, over and above kRequired: the side list's rectangle
		// (Apocrypha Menu Framework 2.3.0, AMF_GetSideListRect) and the calls that make the card a real window in front
		// of that list, so a click or a turn of the wheel over it never reaches the list underneath. Checked once; a
		// framework without any of them gets the page without the card and a one-line note where the switch would be.
		constexpr const char* kLeftPaneExports[] = {
			"AMF_GetSideListRect",
			"igBegin", "igEnd", "igSetNextWindowPos", "igSetNextWindowSize", "igSetNextWindowBgAlpha",
			"igBringWindowToDisplayFront", "igGetCurrentWindow", "igFocusWindow", "igIsWindowFocused",
			"igGetWindowPos", "igGetWindowSize", "igPushStyleVar_Float", "igPushStyleVar_Vec2", "igPopStyleVar",
			"igPushStyleColor_Vec4", "igPopStyleColor", "igGetStyleColorVec4", "igGetTime", "igGetFrameCount",
			"igSetCursorScreenPos", "igTextUnformatted", "igGetTextLineHeightWithSpacing"
		};

		using GetSideListRect_t = bool (*)(float*, float*, float*, float*);

		bool LeftPaneSupported()
		{
			static const bool s_ok = [] {
				for (const char* name : kLeftPaneExports)
				{
					if (!GetMenuFrameworkFunction<void*>(name))
					{
						logger::info("left-pane preview: the loaded menu framework does not export \"{}\" - the switch is "
									 "hidden (needs Apocrypha Menu Framework 2.3.0)", name);
						return false;
					}
				}
				logger::info("left-pane preview: the framework has the side list's rectangle and every window call the card needs");
				return true;
			}();
			return s_ok;
		}

		bool SideListRect(float& a_x0, float& a_y0, float& a_x1, float& a_y1)
		{
			static GetSideListRect_t fn = GetMenuFrameworkFunction<GetSideListRect_t>("AMF_GetSideListRect");
			return fn && fn(&a_x0, &a_y0, &a_x1, &a_y1);
		}

		// Once at the top of each page's render: notice a reopened menu (a gap in the frames the page was drawn),
		// remember the page's window, and apply a DevBench request.
		void NotePage()
		{
			static int s_lastFrame = -10;
			const int frame = LeftPaneSupported() ? ImGuiMCP::GetFrameCount() : 0;
			if (LeftPaneSupported() && s_lastFrame != frame && s_lastFrame < frame - 1)
			{
				// the switch goes back to the INI's default each time the menu opens (the owner, 2026-10-23, as the
				// Witcher 3 Item Explorer's Show the selected item does)
				if (g_leftPane != settings::general::previewInLeftPane)
				{
					logger::info("page: Item preview in the left pane - {} (the menu was opened)",
								 settings::general::previewInLeftPane ? "on" : "off");
				}
				g_leftPane = settings::general::previewInLeftPane;
				g_candidate = nullptr;
				g_lastFocus = nullptr;
				g_mouseLeads = false;
			}
			s_lastFrame = frame;
			if (const int req = g_toolLeftPane.exchange(-1); req >= 0) { g_leftPane = req == 1; }
			g_pageWindow = LeftPaneSupported() ? ImGuiMCP::GetCurrentWindow() : nullptr;
		}

		// The switch, on both pages: "Item preview in the left pane", or the note that the framework is too old.
		void DrawLeftPaneSwitch()
		{
			if (!LeftPaneSupported())
			{
				ImGuiMCP::TextDisabled("%s", strings::TR("AIE_LeftPaneNeeds", "Item preview in the left pane needs Apocrypha Menu Framework 2.3.0"));
				return;
			}
			ImGuiMCP::Toggle(strings::TR("AIE_LeftPane", "Item preview in the left pane"), &g_leftPane);
		}

		// After the rows: which row the preview follows. The highlight wins unless the mouse moved more recently; a
		// new row is taken once it has been the candidate for kFollowRest.
		void ResolvePreview()
		{
			const Catalog::Item* hover = g_hoverItem;
			const Catalog::Item* focus = g_focusItem;
			g_hoverItem = nullptr;
			g_focusItem = nullptr;

			if (auto* io = ImGuiMCP::GetIO())
			{
				if (io->MouseDelta.x != 0.0F || io->MouseDelta.y != 0.0F || io->MouseClicked[0]) { g_mouseLeads = true; }
			}
			if (focus && focus != g_lastFocus) { g_mouseLeads = false; }   // the highlight moved: the controller leads
			g_lastFocus = focus;

			// while the sticks turn the model the highlight cannot move, and the previewed item stays
			if (preview::Handling()) { return; }

			const Catalog::Item* want = g_mouseLeads ? hover : focus;
			if (want && want != g_candidate)
			{
				g_candidate = want;
				g_candidateSince = LeftPaneSupported() ? ImGuiMCP::GetTime() : 0.0;
			}
			if (g_candidate && g_candidate != g_previewItem &&
				(!LeftPaneSupported() || ImGuiMCP::GetTime() - g_candidateSince >= kFollowRest))
			{
				g_previewItem = g_candidate;
			}
		}

		ImGuiMCP::ImU32 Col(int r, int g, int b, int a) { return static_cast<ImGuiMCP::ImU32>((a << 24) | (b << 16) | (g << 8) | r); }

		const char* SoulName(int a_level)
		{
			switch (a_level)
			{
			case 1:  return strings::TR("AIE_Soul1", "Petty");
			case 2:  return strings::TR("AIE_Soul2", "Lesser");
			case 3:  return strings::TR("AIE_Soul3", "Common");
			case 4:  return strings::TR("AIE_Soul4", "Greater");
			case 5:  return strings::TR("AIE_Soul5", "Grand");
			default: return strings::TR("AIE_SoulNone", "empty");
			}
		}

		// Below the square: the description when the item has one, then always its inventory details (the owner,
		// 2026-10-23: "display the description and its inventory information below the preview").
		void DrawItemDetails(const Catalog::Item& a_item)
		{
			iteminfo::Request(a_item.form);
			const iteminfo::Info info = iteminfo::Current();
			if (!info.ready)
			{
				ImGuiMCP::TextDisabled("%s", strings::TR("AIE_Reading", "Reading..."));
				return;
			}
			ImGuiMCP::PushTextWrapPos(0.0F);
			bool any = false;
			if (!info.description.empty())
			{
				ImGuiMCP::TextUnformatted(info.description.c_str());
				any = true;
			}
			for (const auto& line : info.effects)
			{
				ImGuiMCP::TextUnformatted(line.c_str());
				any = true;
			}
			if (any) { ImGuiMCP::Separator(); }

			// the inventory's own figures
			const auto& plugins = Catalog::Plugins();
			ImGuiMCP::TextDisabled("%s - %s", KindLabel(a_item.kind),
								   a_item.pluginIndex < plugins.size() ? plugins[a_item.pluginIndex].fileName.c_str() : "?");
			if (info.armorRating >= 0.0F)
			{
				const char* cls = info.armorClass == iteminfo::ArmorClass::kHeavy  ? strings::TR("AIE_ArmorHeavy", "Heavy armor")
								: info.armorClass == iteminfo::ArmorClass::kLight  ? strings::TR("AIE_ArmorLight", "Light armor")
																				   : strings::TR("AIE_Clothing", "Clothing");
				ImGuiMCP::Text("%s %.0f    %s", strings::TR("AIE_ArmorRating", "Armor rating"), info.armorRating, cls);
			}
			if (info.damage >= 0) { ImGuiMCP::Text("%s %d", strings::TR("AIE_Damage", "Damage"), info.damage); }
			if (info.speed >= 0.0F)
			{
				ImGuiMCP::Text("%s %.2f    %s %.2f", strings::TR("AIE_Speed", "Speed"), info.speed,
							   strings::TR("AIE_Reach", "Reach"), info.reach);
			}
			if (info.critDamage >= 0) { ImGuiMCP::Text("%s %d", strings::TR("AIE_CritDamage", "Critical damage"), info.critDamage); }
			if (info.soulCapacity >= 0)
			{
				ImGuiMCP::Text("%s: %s    %s: %s", strings::TR("AIE_SoulCapacity", "Soul capacity"), SoulName(info.soulCapacity),
							   strings::TR("AIE_SoulHeld", "Soul held"), SoulName(info.soulHeld));
			}
			if (!info.teachesSpell.empty()) { ImGuiMCP::Text("%s: %s", strings::TR("AIE_TeachesSpell", "Teaches the spell"), info.teachesSpell.c_str()); }
			if (!info.teachesSkill.empty()) { ImGuiMCP::Text("%s: %s", strings::TR("AIE_TeachesSkill", "Teaches the skill"), info.teachesSkill.c_str()); }
			char value[32] = "-";
			if (a_item.value >= 0) { std::snprintf(value, sizeof(value), "%d", a_item.value); }
			ImGuiMCP::Text("%s %.1f    %s %s", strings::TR("AIE_Weight", "Weight"), a_item.weight, strings::TR("AIE_Value", "Value"), value);
			if (a_item.questItem) { ImGuiMCP::TextDisabled("%s", strings::TR("AIE_QuestTag", "[quest]")); }
			ImGuiMCP::TextDisabled("%s  0x%08X", a_item.editorID.c_str(), a_item.formID);
			ImGuiMCP::PopTextWrapPos();
		}

		// THE CARD, once at the end of each page's render. Off (or a framework without the side list's rectangle): the
		// helper menu is told to close, which unpauses the game and releases the model.
		void DrawLeftPane()
		{
			ResolvePreview();
			g_cardDrawn = false;

			if (!LeftPaneSupported() || !g_leftPane || !preview::Available())
			{
				preview::SetEnabled(false);
				return;
			}
			float sx0 = 0.0F, sy0 = 0.0F, sx1 = 0.0F, sy1 = 0.0F;
			if (!SideListRect(sx0, sy0, sx1, sy1)) { return; }   // no list drawn this frame: no heartbeat, the menu closes shortly
			g_sideRect[0] = sx0; g_sideRect[1] = sy0; g_sideRect[2] = sx1; g_sideRect[3] = sy1;
			preview::SetEnabled(true);

			// the controller's turn at the model: the sticks while it has them, circle gives them back
			if (preview::Handling())
			{
				preview::HandleController();
				if (ImGuiMCP::IsKeyPressed(ImGuiMCP::ImGuiKey_GamepadFaceRight, false))
				{
					preview::EndHandling();
					g_refocusItem = g_previewItem;   // put the highlight back where it was
				}
			}

			// A REAL WINDOW over the side list, brought to the front every frame: the list underneath never sees a click
			// or the wheel over it. No navigation and no focus when it appears - the highlight stays in the item list.
			ImGuiMCP::SetNextWindowPos(ImGuiMCP::ImVec2{ sx0, sy0 }, ImGuiMCP::ImGuiCond_Always);
			ImGuiMCP::SetNextWindowSize(ImGuiMCP::ImVec2{ sx1 - sx0, sy1 - sy0 }, ImGuiMCP::ImGuiCond_Always);
			ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_WindowBorderSize, 0.0F);
			ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_WindowPadding, ImGuiMCP::ImVec2{ 14.0F, 12.0F });
			ImGuiMCP::ImVec4 bg{ 0.06F, 0.06F, 0.06F, 1.0F };
			if (const ImGuiMCP::ImVec4* themed = ImGuiMCP::GetStyleColorVec4(ImGuiMCP::ImGuiCol_WindowBg)) { bg = *themed; }
			bg.w = 1.0F;   // opaque: the side list must not show through
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_WindowBg, bg);
			ImGuiMCP::SetNextWindowBgAlpha(1.0F);
			const ImGuiMCP::ImGuiWindowFlags flags =
				ImGuiMCP::ImGuiWindowFlags_NoTitleBar | ImGuiMCP::ImGuiWindowFlags_NoResize | ImGuiMCP::ImGuiWindowFlags_NoMove |
				ImGuiMCP::ImGuiWindowFlags_NoCollapse | ImGuiMCP::ImGuiWindowFlags_NoSavedSettings | ImGuiMCP::ImGuiWindowFlags_NoDocking |
				ImGuiMCP::ImGuiWindowFlags_NoScrollbar | ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse |
				ImGuiMCP::ImGuiWindowFlags_NoNav | ImGuiMCP::ImGuiWindowFlags_NoFocusOnAppearing;
			if (ImGuiMCP::Begin("##aie_leftpane", nullptr, flags))
			{
				ImGuiMCP::BringWindowToDisplayFront(ImGuiMCP::GetCurrentWindow());
				ImGuiMCP::ImDrawList* dl = ImGuiMCP::GetWindowDrawList();
				const ImGuiMCP::ImVec2 wp = ImGuiMCP::GetWindowPos();
				const ImGuiMCP::ImVec2 ws = ImGuiMCP::GetWindowSize();
				g_cardRect[0] = wp.x; g_cardRect[1] = wp.y; g_cardRect[2] = wp.x + ws.x; g_cardRect[3] = wp.y + ws.y;

				// the framework's theme frame around the whole card (AMF_DrawThemeFrame), else a plain white line
				using DrawThemeFrame_t = bool (*)(void*, float, float, float, float);
				static DrawThemeFrame_t s_themeFrame = GetMenuFrameworkFunction<DrawThemeFrame_t>("AMF_DrawThemeFrame");
				if (!s_themeFrame || !s_themeFrame(dl, wp.x, wp.y, wp.x + ws.x, wp.y + ws.y))
				{
					const auto line = Col(255, 255, 255, 230);
					ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ wp.x, wp.y }, ImGuiMCP::ImVec2{ wp.x + ws.x, wp.y }, line, 2.0F);
					ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ wp.x + ws.x, wp.y }, ImGuiMCP::ImVec2{ wp.x + ws.x, wp.y + ws.y }, line, 2.0F);
					ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ wp.x + ws.x, wp.y + ws.y }, ImGuiMCP::ImVec2{ wp.x, wp.y + ws.y }, line, 2.0F);
					ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ wp.x, wp.y + ws.y }, ImGuiMCP::ImVec2{ wp.x, wp.y }, line, 2.0F);
				}

				const Catalog::Item* item = g_previewItem;
				// the heading: the item's name, wrapped to the card
				ImGuiMCP::PushTextWrapPos(0.0F);
				if (item) { ImGuiMCP::TextUnformatted(item->name.empty() ? item->editorID.c_str() : item->name.c_str()); }
				else { ImGuiMCP::TextDisabled("%s", strings::TR("AIE_PreviewPickARow", "Point at an item to see it here.")); }
				ImGuiMCP::PopTextWrapPos();

				// THE SQUARE: as wide as the card, unless that would leave the details too little room
				const ImGuiMCP::ImVec2 avail = ImGuiMCP::GetContentRegionAvail();
				const float side = (std::max)(64.0F, (std::min)(avail.x, avail.y * 0.62F));
				const ImGuiMCP::ImVec2 at = ImGuiMCP::GetCursorScreenPos();
				const float qx0 = at.x + (avail.x - side) * 0.5F, qy0 = at.y;
				const float qx1 = qx0 + side, qy1 = qy0 + side;
				g_squareRect[0] = qx0; g_squareRect[1] = qy0; g_squareRect[2] = qx1; g_squareRect[3] = qy1;
				// an invisible button under the square, so a drag on the model is an active item (the card keeps the
				// mouse for the whole turn, and the focus is not handed back in the middle of it)
				ImGuiMCP::SetCursorScreenPos(ImGuiMCP::ImVec2{ qx0, qy0 });
				ImGuiMCP::InvisibleButton("##aie_model", ImGuiMCP::ImVec2{ side, side }, 0);
				if (item) { preview::HandleMouse(qx0, qy0, qx1, qy1); }
				preview::Heartbeat();
				preview::Request(item && item->form ? item->form : nullptr, side, side);
				preview::DrawImage(dl, qx0, qy0, qx1, qy1);
				if (!preview::Available())
				{
					ImGuiMCP::TextDisabled("%s", strings::TR("AIE_PreviewUnavailable", "The 3D preview is not available on this runtime."));
				}

				// below the square: the description and the inventory details, scrolled by the wheel when long
				ImGuiMCP::SetCursorScreenPos(ImGuiMCP::ImVec2{ at.x, qy1 + 8.0F });
				if (ImGuiMCP::BeginChild("##aie_details", ImGuiMCP::ImVec2{ 0.0F, 0.0F }, 0,
										 ImGuiMCP::ImGuiWindowFlags_NoNav))
				{
					if (item) { DrawItemDetails(*item); }
					ImGuiMCP::Spacing();
					ImGuiMCP::PushTextWrapPos(0.0F);
					ImGuiMCP::TextDisabled("%s", strings::TR("AIE_LeftPaneTurnHint",
										   "Drag the item with the left mouse button to turn it; the wheel zooms. On a controller, R3 hands it the sticks and circle gives them back."));
					ImGuiMCP::PopTextWrapPos();
				}
				ImGuiMCP::EndChild();

				// the card never keeps the focus: a click on it (once a drag of the model ends) hands it straight back
				// to the page, so the highlight stays where it was
				if (g_pageWindow && ImGuiMCP::IsWindowFocused(ImGuiMCP::ImGuiFocusedFlags_RootAndChildWindows) &&
					!ImGuiMCP::IsAnyItemActive())
				{
					ImGuiMCP::FocusWindow(g_pageWindow, 0);
				}
				g_cardDrawn = true;
			}
			ImGuiMCP::End();
			ImGuiMCP::PopStyleColor();
			ImGuiMCP::PopStyleVar(2);
		}

		void DrawItemRow(const Catalog::Item& a_item, bool a_showPlugin)
		{
			// The form ID makes every row's widgets unique; without it ImGui merges buttons that
			// share a label and clicking one adds a different item.
		// A fixed buffer rather than std::to_string: this runs for every visible row on every
		// frame, and the id only has to be unique, not pretty.
		char id[16];
		std::snprintf(id, sizeof(id), "%u", a_item.formID);
		ImGuiMCP::PushID(id);


			// How many this row will actually hand over. Gold uses its own amount; a consumable or
			// crafting material uses the slider; anything else is a single item, because fifty
			// cuirasses is never what was meant.
			const bool  gold    = IsGold(a_item);
			const bool  bulk    = a_item.bulk && !gold;
			int*        rowAmount = bulk ? &g_rowCount.try_emplace(a_item.formID, 1).first->second : nullptr;
			const int   rowMax  = gold ? kGoldMax : (bulk ? kStackMax : 1);
			const int   rowWant = gold ? g_goldCount : (bulk ? *rowAmount : 1);

			if (ImGuiMCP::Button(strings::TR("AIE_Add", "Add")))
			{
				const auto count = static_cast<std::uint32_t>(std::clamp(rowWant, 1, rowMax));
				Catalog::GiveToPlayer(a_item.form, count);
				g_status = std::string(strings::TR("AIE_Added", "Added")) + " " +
						   std::to_string(count) + " x " +
						   (a_item.name.empty() ? a_item.editorID : a_item.name);
			}
			NoteRow(a_item);

			// Gold's own amount, shown only on the gold row so the wide range never gets in the way
			// of ordinary items.
			if (gold)
			{
				ImGuiMCP::SameLine();
				ImGuiMCP::PushItemWidth(220.0F);
				precise::SliderInt("##goldamount", &g_goldCount, 1, kGoldMax);
				ImGuiMCP::PopItemWidth();
				if (g_goldCount < 1) { g_goldCount = 1; }
				if (g_goldCount > kGoldMax) { g_goldCount = kGoldMax; }
				NoteRow(a_item);
			}
			// A pile's own amount, on its own row - Browse and Favourites alike (they draw the same rows).
			else if (bulk)
			{
				ImGuiMCP::SameLine();
				ImGuiMCP::PushItemWidth(140.0F);
				precise::SliderInt("##amount", rowAmount, 1, kStackMax);
				ImGuiMCP::PopItemWidth();
				*rowAmount = std::clamp(*rowAmount, 1, kStackMax);
				NoteRow(a_item);
				if (ImGuiMCP::IsItemHovered())
				{
					ImGuiMCP::SetTooltip("%s", strings::TR("AIE_AmountTip", "How many Add gives you"));
				}
			}

			// The favourite toggle. A filled star means it is on the Favourites page; the label is
			// the whole control, so there is nothing to learn.
			ImGuiMCP::SameLine();
			const bool fav = favourites::Contains(a_item.form);
			if (ImGuiMCP::Button(fav ? strings::TR("AIE_FavOn", "[*]") : strings::TR("AIE_FavOff", "[ ]")))
			{
				favourites::Toggle(a_item.form, a_item.name.empty() ? a_item.editorID : a_item.name);
			}
			NoteRow(a_item);
			if (ImGuiMCP::IsItemHovered())
			{
				ImGuiMCP::SetTooltip("%s", fav ? strings::TR("AIE_FavRemoveTip", "Remove from favourites")
											   : strings::TR("AIE_FavAddTip", "Add to favourites"));
			}

			// THE NAME IS A CONTROL, NOT TEXT (the owner, 2026-09-20). It spans the rest of the row, so the
			// mouse anywhere right of the star - on the name or on the grey details drawn over it - is on
			// this Selectable, and the highlight can rest on it.
			ImGuiMCP::SameLine();
			const char* nameText = a_item.name.empty() ? a_item.editorID.c_str() : a_item.name.c_str();
			const float nameStartX = ImGuiMCP::GetCursorPosX();
			const float rowWidth = ImGuiMCP::GetContentRegionAvail().x;
			// 1.2.0: activating it (A, or a click) does nothing more - the preview follows the highlight by itself.
			ImGuiMCP::Selectable(nameText, g_previewItem == &a_item, 0,
								 ImGuiMCP::ImVec2(rowWidth > 0.0F ? rowWidth : 0.0F, 0.0F));
			NoteRow(a_item);
			// The details go back onto the same line, over the Selectable's band: plain text adds no item
			// id, so the Selectable underneath keeps the hover and the click.
			const float detailX = nameStartX + ImGuiMCP::CalcTextSize(nameText).x + 14.0F;
			ImGuiMCP::SameLine(detailX);

			// A quest item is called out wherever it appears, whether or not they are being hidden -
			// handing yourself one can confuse the quest that owns it, and that is worth knowing
			// before you press Add rather than afterwards.
			if (a_item.questItem)
			{
				ImGuiMCP::TextDisabled("%s", strings::TR("AIE_QuestTag", "[quest]"));
				ImGuiMCP::SameLine();
			}
			// No in-game name: the row is showing the editor ID instead, and says so.
			if (a_item.name.empty())
			{
				ImGuiMCP::TextDisabled("%s", strings::TR("AIE_NoNameTag", "(no name)"));
				ImGuiMCP::SameLine();
			}

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
		SyncWithCatalogue();
		NotePage();

		if (!Catalog::Built())
		{
			ImGuiMCP::TextWrapped("%s", strings::TR("AIE_NotBuilt",
				"The catalogue has not been read yet. It lists every plugin your game loaded and "
				"every item each one adds, and it is read once and kept."));
			ImGuiMCP::Spacing();
			if (ImGuiMCP::Button(strings::TR("AIE_Build", "Read the load order")))
			{
				const std::size_t n = Catalog::Build();
				SyncWithCatalogue();
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
			g_selectedPluginName.clear();
			Catalog::Build();
			SyncWithCatalogue();
		}

		ImGuiMCP::Spacing();
		DrawLeftPaneSwitch();
		// The amount is on each pile's own row now (2026-10-02), not one "How many" up here.
		ImGuiMCP::Toggle(strings::TR("AIE_SearchEverywhere", "Search every plugin"), &g_searchEverywhere);
		ImGuiMCP::Toggle(strings::TR("AIE_ShowEnchanted", "Show enchanted variants"), &g_showEnchanted);
		ImGuiMCP::SameLine();
		ImGuiMCP::TextDisabled("%s", strings::TR("AIE_EnchantedHint",
							   "off hides every \"of Cold\" style variant, leaving the base equipment"));

		DrawKindFilters();
		if (g_unnamedChanged)
		{
			// The switch decides what the catalogue HOLDS, so it is applied by re-reading it - here, between the
			// filters and the lists, so nothing drawn this frame uses the old one. Saved first, so the choice survives
			// a restart (rule 16).
			g_unnamedChanged = false;
			const bool saved = settings::Save();
			logger::info("page: Show items with no name {} (saved to the INI: {}) - re-reading the catalogue",
						 settings::general::showUnnamed ? "on" : "off", saved ? "yes" : "no");
			Catalog::Build();
			SyncWithCatalogue();
		}
		ImGuiMCP::Spacing();
		ImGuiMCP::Separator();

		// ---- Search everywhere: one list, no plugin picking ----
		if (g_searchEverywhere)
		{
			ImGuiMCP::PushItemWidth(420.0F);
			ImGuiMCP::Text("%s", strings::TR("AIE_SearchHint", "Search every plugin by name or editor ID"));
			ImGuiMCP::InputText("##search_all", g_itemSearch, sizeof(g_itemSearch));
			ImGuiMCP::PopItemWidth();

			// Memoised: the same question every frame until the player types or flips a switch.
			const auto& hits = Catalog::SearchAllCached(g_itemSearch, g_kind, g_showEnchanted,
												 settings::general::showQuestItems,
												 static_cast<Catalog::Sort>(settings::general::sortMode),
												 kSearchLimit);
			if (g_itemSearch[0] == '\0')
			{
				ImGuiMCP::TextDisabled("%s", strings::TR("AIE_TypeToSearch", "Type to search."));
				DrawLeftPane();
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
			DrawLeftPane();

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

		static std::string s_nameLower;
		if (ImGuiMCP::BeginChild("aie_plugins", ImGuiMCP::ImVec2(320.0F, 420.0F), 1))
		{
			for (std::size_t i = 0; i < plugins.size(); ++i)
			{
				const auto& p = plugins[i];
				if (!filterLower.empty())
				{
					// Lower-cased into a REUSED buffer rather than a fresh string per plugin per
					// frame - a couple of hundred allocations a frame for a filter usually empty.
					s_nameLower.assign(p.fileName);
					std::transform(s_nameLower.begin(), s_nameLower.end(), s_nameLower.begin(),
								   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (s_nameLower.find(filterLower) == std::string::npos) { continue; }
				}
				// A plugin whose only items have no name stays in the list while they are hidden, so its view can say
				// why it shows nothing (EldenSkyrim.esp-like mods would otherwise vanish from the page altogether).
				if (p.itemCount == 0 && p.namelessFound == p.namelessListed) { continue; }

				const std::string label = p.fileName + "  (" + std::to_string(p.itemCount) + ")" +
										  (p.light ? "  [ESL]" : "") + "##p" + std::to_string(i);
				if (ImGuiMCP::Selectable(label.c_str(), g_selectedPlugin == static_cast<int>(i)))
				{
					g_selectedPlugin = static_cast<int>(i);
					g_selectedPluginName = p.fileName;
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
				// Memoised - see the note on the search above. Selecting Skyrim.esm put twenty
				// thousand items through a filter and a sort on every frame the page drew.
				const auto& items = Catalog::ItemsOfCached(static_cast<std::uint32_t>(g_selectedPlugin),
													g_itemSearch, g_kind, g_showEnchanted,
													settings::general::showQuestItems,
													static_cast<Catalog::Sort>(settings::general::sortMode));
				ImGuiMCP::TextDisabled("%s - %d %s", plugins[g_selectedPlugin].fileName.c_str(),
									   static_cast<int>(items.size()), strings::TR("AIE_Shown", "shown"));

				// Nobody is left wondering where a mod's items went: with the switch off, say how many nameless items
				// this plugin has and how to list them. Counted during the build - no file is read for this.
				const auto& sel = plugins[g_selectedPlugin];
				if (!settings::general::showUnnamed && sel.namelessFound > sel.namelessListed)
				{
					char hidden[512];
					std::snprintf(hidden, sizeof(hidden),
								  strings::TR("AIE_NoNameHidden",
											  "%d items here have no name and are hidden - turn on Show items with no name to list them"),
								  static_cast<int>(sel.namelessFound - sel.namelessListed));
					ImGuiMCP::PushTextWrapPos(0.0F);   // the item column is narrow; the line must wrap, not run off it
					ImGuiMCP::TextDisabled("%s", hidden);
					ImGuiMCP::PopTextWrapPos();
				}

				for (const auto* item : items) { DrawItemRow(*item, false); }
			}
		}
		ImGuiMCP::EndChild();
		DrawLeftPane();

		if (!g_status.empty())
		{
			ImGuiMCP::Spacing();
			ImGuiMCP::TextDisabled("%s", g_status.c_str());
		}

	}

	// The favourites page. Deliberately a SECOND page rather than a filter on the first: the whole
	// point is a short list you keep coming back to, and hiding it behind the same search box the
	// catalogue uses would defeat that.
	void __stdcall FavouritesPanel::Render()
	{
		strings::Tick();
		SyncWithCatalogue();
		NotePage();

		ImGuiMCP::Text("%s", strings::TR("AIE_FavTitle", "Favourites"));
		DrawLeftPaneSwitch();
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

		// Save, beside the count. Every star already writes the file the moment it is pressed, so
		// this changes nothing mechanically - it is the reassurance that the list on screen is the
		// list on disk (the owner, 2026-09-19), and the same control the framework's own menu-list
		// page carries for the same reason.
		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button(strings::TR("AIE_FavSave", "Save favourites")))
		{
			g_favSaved = favourites::Save();
			g_favSavedAt = ImGuiMCP::GetIO() ? ImGuiMCP::GetIO()->DeltaTime : 0.0F;
			g_favSavedShow = 180;   // about three seconds at 60 fps
		}
		if (g_favSavedShow > 0)
		{
			--g_favSavedShow;
			ImGuiMCP::SameLine();
			ImGuiMCP::TextDisabled("%s", g_favSaved ? strings::TR("AIE_FavSaved", "saved")
													: strings::TR("AIE_FavSaveFailed", "could not be written - see the log"));
		}
		ImGuiMCP::SameLine();
		ImGuiMCP::TextDisabled("%s", strings::TR("AIE_FavWhere", "kept in Documents/My Games/Skyrim Special Edition/SKSE/, per plugin, so they survive a load-order change"));

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
		DrawLeftPane();

        if (!g_status.empty())
        {
            ImGuiMCP::Spacing();
            ImGuiMCP::TextDisabled("%s", g_status.c_str());
        }

	}

	void PreviewByFormID(std::uint32_t a_formID)
	{
		auto* form = RE::TESForm::LookupByID(a_formID);
		g_previewItem = form ? Catalog::Find(form) : nullptr;
		// held until the highlight (or a moving mouse) goes to another row
		g_candidate = g_previewItem;
	}

	void SetLeftPaneFromTool(bool a_on) { g_toolLeftPane = a_on ? 1 : 0; }

	LeftPaneState GetLeftPaneState()
	{
		LeftPaneState st;
		st.supported = LeftPaneSupported();
		st.on = g_leftPane;
		st.iniDefault = settings::general::previewInLeftPane;
		st.cardDrawn = g_cardDrawn.load();
		st.mouseLeads = g_mouseLeads;
		st.candidateFormID = g_candidate ? g_candidate->formID : 0;
		st.previewFormID = g_previewItem ? g_previewItem->formID : 0;
		for (int i = 0; i < 4; ++i) { st.card[i] = g_cardRect[i].load(); st.side[i] = g_sideRect[i].load(); st.square[i] = g_squareRect[i].load(); }
		const auto info = iteminfo::Current();
		st.infoReady = info.ready;
		st.infoFormID = info.formID;
		st.descriptionChars = info.description.size();
		st.descriptionStart = info.description.substr(0, 120);
		st.effectLines = info.effects.size();
		st.armorRating = info.armorRating;
		st.damage = info.damage;
		return st;
	}

	std::uint32_t PreviewedFormID() { return g_previewItem ? g_previewItem->formID : 0; }
}
