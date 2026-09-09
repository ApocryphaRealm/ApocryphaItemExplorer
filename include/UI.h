#pragma once

namespace UI
{
	// Adds this mod's page to the menu framework's Mod Control Panel (Apocrypha Menu Framework
	// preferred, stock SKSE Menu Framework as the fallback). Call at kDataLoaded.
	void Register();

	namespace ExplorerPanel
	{
		void __stdcall Render();
	}

	// A second page under the same section, for the items marked with the star on the first.
	namespace FavouritesPanel
	{
		void __stdcall Render();
	}
}
