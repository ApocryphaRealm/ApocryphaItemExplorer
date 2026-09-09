#include "PCH.h"

#include "Preview.h"

#include "Settings.h"

#include "utils/Logger.h"

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

		// Every frame while something is shown - this is what actually puts it on screen.
		mgr->Render();
	}

	RE::TESForm* Current() { return g_loaded; }

	bool Available() { return RE::Inventory3DManager::GetSingleton() != nullptr; }

	Status GetStatus()
	{
		Status s;
		s.available = Available();
		s.showing = g_loaded != nullptr;
		s.currentFormID = g_loaded ? g_loaded->GetFormID() : 0;
		s.loads = g_loads;
		s.failures = g_failures;
		return s;
	}
}
