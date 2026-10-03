#include "PCH.h"

#include "UI.h"

#include "SKSEMenuFramework.h"

#include "Catalog.h"
#include "PreciseSlider.h"
#include "Favourites.h"
#include "Preview.h"
#include "Settings.h"

#include "utils/Logger.h"
#include "utils/Strings.h"
#include "utils/Toggle.h"

#include <unordered_map>

#include <algorithm>
#include <array>
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

		// THE FIXED ITEM (the owner, 2026-09-19): "the mouse should select the list item which fixes
		// the item to the preview and then they can zoom and rotate ... but rotate and zoom only on
		// activating the item with a click". Two states, not one:
		//   nothing fixed - the preview FOLLOWS the mouse, so you can see what you scroll past, and
		//                   turning and zooming do nothing (there is nothing to take hold of);
		//   fixed         - the clicked item stays in the pane whatever the mouse passes over, and
		//                   the mouse and the sticks can handle it.
		// Clicking another row moves the fix to that row; R3 on the pad does the same thing.
		const Catalog::Item* g_fixedItem = nullptr;

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
			// 1.1.1: the name is a Selectable that spans the row, placed by hand
			"igGetContentRegionAvail", "igGetCursorPosX",
			"igInvisibleButton", "igIsItemHovered",
			"ImDrawList_AddRectFilled", "ImDrawList_AddCircleFilled",
			// the 3D preview's floating box: its frame, caption and picture are drawn on the framework's
			// SCREEN-WIDE foreground list, which arrived in Apocrypha Menu Framework 1.5.8.
			// A framework older than that is refused here rather than met with a null call.
			"igGetIO", "igGetForegroundDrawList_Nil",
			"ImDrawList_AddLine", "ImDrawList_AddText_Vec2",
			"igSliderFloat", "igSliderInt", "igIsKeyDown_Nil", "igIsMouseDown_Nil", "igCombo_Str_arr",
			// 1.0.7: the 3D preview pane is an image of the engine's own render, and the row under
			// the cursor OR under D-pad focus is what it shows.
			"igIsItemFocused", "ImDrawList_AddImage", "igCalcTextSize",
			// 1.1.1: R3 takes hold of the previewed item and circle lets go.
			"igIsKeyPressed_Bool",
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
			// HOVER BEATS FOCUS, AND ONLY ONE OF THEM WINS PER FRAME (2026-09-19).
			//
			// This used to set the previewed item when a row was hovered OR focused, with no
			// precedence - so the controller's focus (which stays where it was left) and the mouse's
			// hover both claimed it every frame and whichever row was drawn last won. Clicking a row
			// with the mouse therefore did nothing: the preview snapped back to whatever the D-pad
			// had last selected (the owner, 2026-09-19: "it just goes back to whatever I last
			// selected with controller"). Worse, the two fought frame by frame, so the requested
			// item CHANGED every frame - which unloads and reloads the engine's model continuously
			// and never lets a capture finish, which is why items "appear for an instant and then
			// they're gone".
			//
			// The rule now: if the mouse is over any row this frame, the mouse decides; otherwise
			// the highlight does. Recorded per frame and applied after the list is drawn.
			if (ImGuiMCP::IsItemHovered())
			{
				g_hoverItem = &a_item;
				// A click anywhere on the row fixes it - the Add button, the star, the name. The
				// button's own action still happens; fixing is in addition to it, not instead.
				if (auto* io = ImGuiMCP::GetIO(); io && io->MouseClicked[0]) { g_fixedItem = &a_item; }
			}
			else if (ImGuiMCP::IsItemFocused() && g_focusItem == nullptr) { g_focusItem = &a_item; }

			// R3 ON THE HIGHLIGHTED ROW hands the preview box the sticks (the owner, 2026-09-19).
			// Asked of the row that has the highlight, so the item being handled is the one the
			// player was looking at - and while the box has the sticks the highlight cannot move,
			// so it is still on this row when circle gives them back.
			if (ImGuiMCP::IsItemFocused() && !preview::Handling() &&
				ImGuiMCP::IsKeyPressed(ImGuiMCP::ImGuiKey_GamepadR3, false))
			{
				g_previewItem = &a_item;
				g_fixedItem = &a_item;   // R3 is the controller's "click": it fixes the item too
				preview::BeginHandling();
			}
		}

		// The floating preview box: the heartbeat that keeps the helper menu open, the request for
		// whatever row is under the cursor, and the box itself, drawn in front of this window. One
		// call at the end of each page's render.
		void DrawPreviewFloating()
		{
			// One winner for the frame. Doing this here, after the rows, is what stops the request
			// changing several times within a single frame.
			if (g_fixedItem) { g_previewItem = g_fixedItem; }
			else if (g_hoverItem) { g_previewItem = g_hoverItem; }
			else if (g_focusItem) { g_previewItem = g_focusItem; }
			g_hoverItem = nullptr;
			g_focusItem = nullptr;

			if (!settings::general::show3DPreview || !preview::Available())
			{
				// The box is gone; it cannot still be holding the pad.
				preview::EndHandling();
				return;
			}
			float x0 = 0.0F, y0 = 0.0F, x1 = 0.0F, y1 = 0.0F;
			if (!preview::PaneRect(x0, y0, x1, y1)) { return; }
			// The box takes the mouse before anything is drawn, so a turn or a zoom asked for this
			// frame is already in the capture this frame produces.
			// Turning and zooming need a FIXED item. While the preview is merely following the
			// mouse there is nothing to take hold of, and letting the wheel zoom a preview that is
			// about to change under it reads as the controls being broken.
			if (g_fixedItem) { preview::HandleMouse(x0, y0, x1, y1); }
			// The controller's turn at the box: the sticks while it has them, circle to give them
			// back and go on navigating the list from the row the highlight never left.
			if (preview::Handling())
			{
				preview::HandleController();
				if (ImGuiMCP::IsKeyPressed(ImGuiMCP::ImGuiKey_GamepadFaceRight, false))
				{
					preview::EndHandling();
					g_refocusItem = g_previewItem;   // put the highlight back where it was
				}
			}
			preview::Heartbeat();
			preview::Request(g_previewItem && g_previewItem->form ? g_previewItem->form : nullptr, x1 - x0, y1 - y0);
			const char* title = nullptr;
			if (g_previewItem) { title = g_previewItem->name.empty() ? g_previewItem->editorID.c_str() : g_previewItem->name.c_str(); }
			preview::DrawFloating(title);
		}

		// Where the box sits: the earlier version's sliders (the owner, 2026-09-18: the SkyHUD
		// Settings Menu way of moving things - a position you set, shown live).
		void DrawPreviewSettings()
		{
			if (!settings::general::show3DPreview) { return; }
			ImGuiMCP::TextDisabled("%s", strings::TR("AIE_PreviewWhere", "Where the preview box sits - it floats in front of this window, so put it anywhere"));
			ImGuiMCP::PushItemWidth(220.0F);
			precise::SliderFloat(strings::TR("AIE_PreviewX", "Pane across"), &settings::preview::paneX, 0.05F, 0.95F, "%.2f");
			precise::SliderFloat(strings::TR("AIE_PreviewY", "Pane down"), &settings::preview::paneY, 0.05F, 0.95F, "%.2f");
			precise::SliderFloat(strings::TR("AIE_PreviewSize", "Pane size"), &settings::preview::paneSize, 0.08F, 0.90F, "%.2f");
			ImGuiMCP::PopItemWidth();
			ImGuiMCP::TextDisabled("%s", strings::TR("AIE_PreviewMouseHint",
								   "Click an item in the list to fix it here - then hold the left mouse button on it to turn it, and scroll the wheel to zoom. Without a click the preview just follows the mouse."));
			ImGuiMCP::TextDisabled("%s", strings::TR("AIE_PreviewPadHint",
								   "Controller: press R3 on an item to take hold of it - the left stick turns it, the right "
								   "stick zooms, and circle lets go and puts you back on the list."));
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

			// THE NAME IS A CONTROL, NOT TEXT (the owner, 2026-09-20). It spans the rest of the row, so a
			// click anywhere right of the star - on the name or on the grey details drawn over it - lands
			// on this Selectable. Activating it, by mouse or by the controller's A, FIXES the item in the
			// preview; that is the whole gesture, with nothing to learn.
			ImGuiMCP::SameLine();
			const char* nameText = a_item.name.empty() ? a_item.editorID.c_str() : a_item.name.c_str();
			const float nameStartX = ImGuiMCP::GetCursorPosX();
			const float rowWidth = ImGuiMCP::GetContentRegionAvail().x;
			if (ImGuiMCP::Selectable(nameText, g_previewItem == &a_item, 0,
									 ImGuiMCP::ImVec2(rowWidth > 0.0F ? rowWidth : 0.0F, 0.0F)))
			{
				g_fixedItem = &a_item;
			}
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
				g_previewItem = nullptr;
				g_fixedItem = nullptr;
				g_refocusItem = nullptr;
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
			g_previewItem = nullptr;
			g_fixedItem = nullptr;
			g_refocusItem = nullptr;
		}

		ImGuiMCP::Spacing();
		// The amount is on each pile's own row now (2026-10-02), not one "How many" up here.
		ImGuiMCP::Toggle(strings::TR("AIE_SearchEverywhere", "Search every plugin"), &g_searchEverywhere);
		ImGuiMCP::Toggle(strings::TR("AIE_ShowEnchanted", "Show enchanted variants"), &g_showEnchanted);
		ImGuiMCP::SameLine();
		ImGuiMCP::TextDisabled("%s", strings::TR("AIE_EnchantedHint",
							   "off hides every \"of Cold\" style variant, leaving the base equipment"));

		DrawKindFilters();
		ImGuiMCP::Spacing();
		DrawPreviewSettings();
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
				DrawPreviewFloating();
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
			DrawPreviewFloating();

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
				// Memoised - see the note on the search above. Selecting Skyrim.esm put twenty
				// thousand items through a filter and a sort on every frame the page drew.
				const auto& items = Catalog::ItemsOfCached(static_cast<std::uint32_t>(g_selectedPlugin),
													g_itemSearch, g_kind, g_showEnchanted,
													settings::general::showQuestItems,
													static_cast<Catalog::Sort>(settings::general::sortMode));
				ImGuiMCP::TextDisabled("%s - %d %s", plugins[g_selectedPlugin].fileName.c_str(),
									   static_cast<int>(items.size()), strings::TR("AIE_Shown", "shown"));

				for (const auto* item : items) { DrawItemRow(*item, false); }
			}
		}
		ImGuiMCP::EndChild();
		DrawPreviewFloating();

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
		DrawPreviewFloating();

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
	}

	std::uint32_t PreviewedFormID() { return g_previewItem ? g_previewItem->formID : 0; }
}
