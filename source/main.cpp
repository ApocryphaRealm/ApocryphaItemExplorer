// ApocryphaRealm Item Explorer - own code, GPL-3.0-or-later (2026-09-08).
//
// Browse every plugin the game loaded, see the items each one adds, and take any of them. It is
// built from scratch on the Apocrypha Menu Framework, with AddItemMenu - Ultimate Mod Explorer by
// towawot as the reference for WHAT the finished thing should do. No code or assets from that mod
// are used, and its Papyrus sources were deliberately never opened - its permissions require the
// author's consent to modify its files or use its assets, so this is a clean-room build.
//
// Three things it does differently, and each is the reason it is worth building rather than
// converting: it needs no UIExtensions, it runs no Papyrus, and it ships no ESP.
#include "PCH.h"

#include "Favourites.h"
#include "Catalog.h"
#include "DevBenchTool.h"
#include "Settings.h"
#include "PreviewMenu.h"
#include "UI.h"

#include "utils/AddressLibraryGuard.h"
#include "utils/Logger.h"
#include "utils/Strings.h"

namespace
{
	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type)
		{
		case SKSE::MessagingInterface::kPostLoad:
			DevBenchTool::Init(false);
			break;

		case SKSE::MessagingInterface::kDataLoaded:
			strings::Configure("ItemExplorer");
			favourites::Resolve();
			UI::Register();
			preview::RegisterPreviewMenu();
			DevBenchTool::Init(true);
			// The catalogue is NOT built here. Walking every form array costs real time on a large
			// load order, and nothing needs the answer until someone opens the page or asks the
			// DevBench tool - so it is built on demand and cached.
			logger::info("ready; the catalogue is built the first time it is asked for");
			break;

		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SKSE::log::init("ItemExplorer");

	// 1.0.7: before anything resolves an address, say which Address Library file this game needs and whether it
	// is there; when it is missing the plugin loads inert with a message that names the file (the guard every
	// mod of ours carries since Wheeler 1.2.9).
	if (!AddressLibraryGuard::Guard("Item Explorer"))
	{
		return true;
	}

	settings::Init("ItemExplorer.ini");
	settings::ApplyLogLevel();

	favourites::Load();

	logger::info("ApocryphaRealm Item Explorer {} loading",
				 SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));

	SKSE::GetMessagingInterface()->RegisterListener(MessageHandler);

	return true;
}
