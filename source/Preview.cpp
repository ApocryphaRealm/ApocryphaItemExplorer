#include "PCH.h"

#include "Preview.h"

#include "SKSEMenuFramework.h"

#include "Settings.h"

#include "utils/Logger.h"
#include "utils/Strings.h"

#include <algorithm>

namespace preview
{
	namespace
	{
		RE::TESForm* g_requested = nullptr;   // what the page wants shown
		RE::TESForm* g_loaded = nullptr;      // what the game currently holds a model for
		bool         g_begun = false;         // Begin3D has been called and End3D has not
		std::uint32_t g_loads = 0;
		std::uint32_t g_failures = 0;
		bool         g_loggedUnavailable = false;

		// The RAW override, used to calibrate the mapping: a driving tool sweeps these and the
		// numbers that land the model inside the pane become the INI's mapping constants. Zero
		// scale means "no override" - the placement is derived from the pane instead.
		float g_rawX = 0.0F, g_rawY = 0.0F, g_rawZ = 0.0F, g_rawScale = 0.0F;

		// The last display size the framework reported. Kept so the settings page and the DevBench
		// tool can answer "where is the pane, in pixels?" on a frame of their own.
		float g_displayW = 0.0F, g_displayH = 0.0F;
		bool  g_frameDrawn = false;

		// What Tick() last handed the 3D manager, whichever path produced it - reported by the
		// DevBench tool so a calibration sweep can read back what it actually set.
		float g_appliedX = 0.0F, g_appliedY = 0.0F, g_appliedZ = 0.0F, g_appliedScale = 0.0F;

		// ImGui packs a colour as A<<24 | B<<16 | G<<8 | R.
		constexpr ImGuiMCP::ImU32 Col(int a_r, int a_g, int a_b, int a_a)
		{
			return static_cast<ImGuiMCP::ImU32>((a_a << 24) | (a_b << 16) | (a_g << 8) | a_r);
		}

		RE::Inventory3DManager* Manager()
		{
			auto* mgr = RE::Inventory3DManager::GetSingleton();

			// Rule 30: asked for, not assumed - and rule 17, a miss now is not a miss forever, so
			// this is not cached and not made fatal. It is logged once so a page that shows nothing
			// can be explained without guessing.
			if (!mgr && !g_loggedUnavailable)
			{
				g_loggedUnavailable = true;
				logger::warn("preview: the game's inventory 3D manager is not available; the item "
							 "preview will stay empty");
			}
			return mgr;
		}

		void Stop(RE::Inventory3DManager* a_mgr)
		{
			if (!a_mgr) { return; }

			if (g_loaded)
			{
				a_mgr->UnloadInventoryItem();
				g_loaded = nullptr;
			}
			if (g_begun)
			{
				a_mgr->End3D();
				g_begun = false;
			}
		}

		// The pane, in display pixels. Square, measured against the screen HEIGHT so it keeps its
		// shape on an ultrawide.
		bool PaneRect(float& a_x0, float& a_y0, float& a_x1, float& a_y1)
		{
			if (g_displayW <= 0.0F || g_displayH <= 0.0F) { return false; }
			const float side = settings::preview::paneSize * g_displayH;
			const float cx = settings::preview::paneX * g_displayW;
			const float cy = settings::preview::paneY * g_displayH;
			a_x0 = cx - side * 0.5F;
			a_y0 = cy - side * 0.5F;
			a_x1 = cx + side * 0.5F;
			a_y1 = cy + side * 0.5F;
			return true;
		}
	}

	void Show(RE::TESForm* a_form)
	{
		g_requested = a_form;
	}

	void Hide()
	{
		g_requested = nullptr;

		// Torn down immediately rather than on the next Tick: Hide() is what the page calls when it
		// stops being drawn, and there may not BE a next tick.
		Stop(RE::Inventory3DManager::GetSingleton());
	}

	void Tick()
	{
		if (!settings::general::show3DPreview)
		{
			if (g_loaded || g_begun) { Stop(RE::Inventory3DManager::GetSingleton()); }
			return;
		}

		auto* mgr = Manager();
		if (!mgr) { return; }

		if (!g_requested)
		{
			Stop(mgr);
			return;
		}

		if (g_requested != g_loaded)
		{
			// A bound object is what the manager takes. Spells and other non-bound forms have no
			// inventory model, so they are declined rather than pushed in and hoped for.
			auto* bound = g_requested->As<RE::TESBoundObject>();
			if (!bound)
			{
				if (g_loaded) { Stop(mgr); }
				g_requested = nullptr;
				return;
			}

			if (!g_begun)
			{
				mgr->Begin3D(RE::INTERFACE_LIGHT_SCHEME::kInventory);
				g_begun = true;
			}
			else if (g_loaded)
			{
				mgr->UnloadInventoryItem();
				g_loaded = nullptr;
			}

			mgr->LoadInventoryItem(bound, nullptr);
			g_loaded = g_requested;
			++g_loads;

			logger::debug("preview: showing {:08X} \"{}\"", g_loaded->GetFormID(), g_loaded->GetName());
		}

		// Placement, re-applied every frame because the manager owns these and the inventory
		// resets them when it runs.
		//
		// Two paths. The RAW override is the calibration path - a driving tool sets exact numbers
		// and reads back where the model landed. Otherwise the placement is DERIVED from the pane
		// the player positioned, through the linear mapping in the INI: the pane centre, in screen
		// fractions, becomes a horizontal and a vertical offset at a fixed depth. The constants
		// carry the signs, so an axis that runs the other way is an INI change, not a code change.
		float x = g_rawX, y = g_rawY, z = g_rawZ, scale = g_rawScale;
		if (g_rawScale <= 0.0F)
		{
			x = settings::preview::mapDepth;
			y = settings::preview::mapBaseY + (settings::preview::paneX - 0.5F) * settings::preview::mapSpanX;
			z = settings::preview::mapBaseZ + (0.5F - settings::preview::paneY) * settings::preview::mapSpanY;
			scale = settings::preview::mapScale * (settings::preview::paneSize / 0.28F);
		}

		if (scale > 0.0F)
		{
			mgr->itemPos = RE::NiPoint3(x, y, z);
			mgr->itemPosCopy = mgr->itemPos;
			mgr->itemScale = scale;
			mgr->itemScaleCopy = scale;
		}
		g_appliedX = x; g_appliedY = y; g_appliedZ = z; g_appliedScale = scale;

		// Every frame while something is shown - this is what actually puts it on screen.
		mgr->Render();
	}

	void DrawFrame()
	{
		if (!settings::general::show3DPreview || !settings::preview::showFrame) { return; }

		ImGuiMCP::ImGuiIO* io = ImGuiMCP::GetIO();
		if (!io) { return; }
		g_displayW = io->DisplaySize.x;
		g_displayH = io->DisplaySize.y;

		float x0 = 0.0F, y0 = 0.0F, x1 = 0.0F, y1 = 0.0F;
		if (!PaneRect(x0, y0, x1, y1)) { return; }

		// The FOREGROUND list, so the frame sits over the framework window rather than under it.
		// The model itself cannot be lifted that way - it is drawn earlier in the frame, by the
		// game - so nothing is painted inside these lines. A fill here would hide what it frames.
		ImGuiMCP::ImDrawList* dl = ImGuiMCP::GetForegroundDrawList();
		if (!dl) { return; }

		constexpr ImGuiMCP::ImU32 kLine = Col(198, 168, 109, 210);  // the framework parchment gold
		constexpr ImGuiMCP::ImU32 kText = Col(232, 220, 196, 235);
		constexpr ImGuiMCP::ImU32 kDim = Col(232, 220, 196, 140);

		// Corner brackets rather than a closed box: they mark the area without drawing a line
		// across anything, and they read as a viewport instead of a window.
		const float len = (x1 - x0) * 0.18F;
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x0, y0 }, ImGuiMCP::ImVec2{ x0 + len, y0 }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x0, y0 }, ImGuiMCP::ImVec2{ x0, y0 + len }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x1, y0 }, ImGuiMCP::ImVec2{ x1 - len, y0 }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x1, y0 }, ImGuiMCP::ImVec2{ x1, y0 + len }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x0, y1 }, ImGuiMCP::ImVec2{ x0 + len, y1 }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x0, y1 }, ImGuiMCP::ImVec2{ x0, y1 - len }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x1, y1 }, ImGuiMCP::ImVec2{ x1 - len, y1 }, kLine, 2.0F);
		ImGuiMCP::ImDrawListManager::AddLine(dl, ImGuiMCP::ImVec2{ x1, y1 }, ImGuiMCP::ImVec2{ x1, y1 - len }, kLine, 2.0F);

		// The caption sits ABOVE the pane, never inside it.
		const char* caption = nullptr;
		ImGuiMCP::ImU32 captionCol = kText;
		if (!Available())
		{
			caption = strings::TR("AIE_PreviewNoRenderer", "3D preview unavailable on this runtime");
			captionCol = kDim;
		}
		else if (g_loaded)
		{
			caption = g_loaded->GetName();
			if (!caption || !caption[0]) { caption = strings::TR("AIE_PreviewUnnamed", "(no name)"); }
		}
		else
		{
			caption = strings::TR("AIE_PreviewPickARow", "Click a row to preview it here");
			captionCol = kDim;
		}
		ImGuiMCP::ImDrawListManager::AddText(dl, ImGuiMCP::ImVec2{ x0, y0 - 18.0F }, captionCol, caption, nullptr);

		g_frameDrawn = true;
	}

	RE::TESForm* Current() { return g_loaded; }

	void SetPlacement(float a_x, float a_y, float a_z, float a_scale)
	{
		g_rawX = a_x; g_rawY = a_y; g_rawZ = a_z; g_rawScale = a_scale;
		logger::info("preview: raw placement override {} to ({:.1f}, {:.1f}, {:.1f}) scale {:.3f}",
					 a_scale > 0.0F ? "set" : "cleared", a_x, a_y, a_z, a_scale);
	}

	void GetPlacement(float& a_x, float& a_y, float& a_z, float& a_scale)
	{
		a_x = g_rawX; a_y = g_rawY; a_z = g_rawZ; a_scale = g_rawScale;
	}

	void SetPane(float a_centreX, float a_centreY, float a_size)
	{
		// The settings ARE the state - nothing is cached here, so "Restore defaults" and a reload
		// of the INI both take effect on the next frame without a second copy to keep in step.
		// Clamped rather than trusted: a pane centred off-screen, or sized to nothing, is a preview
		// nobody can find and looks exactly like a broken renderer.
		settings::preview::paneX = std::clamp(a_centreX, 0.05F, 0.95F);
		settings::preview::paneY = std::clamp(a_centreY, 0.05F, 0.95F);
		settings::preview::paneSize = std::clamp(a_size, 0.08F, 0.90F);
		logger::debug("preview: pane set to ({:.3f}, {:.3f}) size {:.3f}", settings::preview::paneX,
					  settings::preview::paneY, settings::preview::paneSize);
	}

	bool GetPaneRect(float& a_x0, float& a_y0, float& a_x1, float& a_y1)
	{
		return PaneRect(a_x0, a_y0, a_x1, a_y1);
	}

	bool Available() { return RE::Inventory3DManager::GetSingleton() != nullptr; }

	Status GetStatus()
	{
		Status s;
		s.available = Available();
		s.showing = g_loaded != nullptr;
		s.currentFormID = g_loaded ? g_loaded->GetFormID() : 0;
		s.loads = g_loads;
		s.failures = g_failures;
		PaneRect(s.paneX0, s.paneY0, s.paneX1, s.paneY1);
		s.posX = g_appliedX; s.posY = g_appliedY; s.posZ = g_appliedZ; s.scale = g_appliedScale;
		s.rawOverride = g_rawScale > 0.0F;
		s.frameDrawn = g_frameDrawn;
		return s;
	}
}
