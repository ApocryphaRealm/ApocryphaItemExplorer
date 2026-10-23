#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

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

	// For the DevBench tool: point the 3D preview at a form without a mouse.
	void PreviewByFormID(std::uint32_t a_formID);
	[[nodiscard]] std::uint32_t PreviewedFormID();

	// For the DevBench tool: the left-pane card (1.2.0). The switch is flipped on the page's next frame, as the page's
	// own switch would be; the state is what the page drew last frame.
	void SetLeftPaneFromTool(bool a_on);
	struct LeftPaneState
	{
		bool          supported = false;     // the framework exports AMF_GetSideListRect and the window calls
		bool          on = false;            // the switch
		bool          iniDefault = false;    // bPreviewInLeftPane - what the switch starts at when the menu opens
		bool          cardDrawn = false;     // the card window was drawn last frame
		bool          mouseLeads = false;    // the mouse moved more recently than the highlight
		std::uint32_t candidateFormID = 0;   // the row being rested on
		std::uint32_t previewFormID = 0;     // the row the card shows
		float         card[4]{}, side[4]{}, square[4]{};
		bool          infoReady = false;
		std::uint32_t infoFormID = 0;
		std::size_t   descriptionChars = 0;
		std::string   descriptionStart;
		std::size_t   effectLines = 0;
		float         armorRating = -1.0F;
		int           damage = -1;
	};
	[[nodiscard]] LeftPaneState GetLeftPaneState();
}
