#include "PCH.h"

#include "DevBenchTool.h"

#include "Catalog.h"
#include "UI.h"
#include "Preview.h"
#include "DevBench/DevBenchAPI.h"
#include "Settings.h"

#include "utils/Logger.h"

#include <format>
#include <string>
#include <string_view>

namespace DevBenchTool
{
	namespace
	{
		std::string EscapeJson(std::string_view a_in)
		{
			std::string out;
			out.reserve(a_in.size() + 8);
			for (const char c : a_in)
			{
				switch (c)
				{
				case '\\': out += "\\\\"; break;
				case '"': out += "\\\""; break;
				case '\n': out += "\\n"; break;
				case '\r': break;
				case '\t': out += "\\t"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20) { break; }
					out += c;
					break;
				}
			}
			return out;
		}

		// Pulls the text after "<key>" out of the raw args, up to the closing quote.
		[[nodiscard]] std::string After(std::string_view a_args, const char* a_key)
		{
			const std::string_view key{ a_key };
			const auto at = a_args.find(key);
			if (at == std::string_view::npos) { return {}; }
			std::string rest{ a_args.substr(at + key.size()) };
			const auto quote = rest.find('"');
			if (quote != std::string::npos) { rest = rest.substr(0, quote); }
			return rest;
		}

		void EnsureBuilt()
		{
			if (!Catalog::Built()) { Catalog::Build(); }
		}

		void ControlTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const std::string_view args = a_argsJson ? a_argsJson : "";
			auto has = [&](const char* a_op) { return args.find(a_op) != std::string_view::npos; };

			// op=build - force a rebuild and report the totals.
			if (has("\"build\""))
			{
				const std::size_t n = Catalog::Build();
				a_write(a_sink, std::format(
					"{{\"ok\":true,\"op\":\"build\",\"items\":{},\"plugins\":{}}}",
					n, Catalog::Plugins().size()).c_str());
				return;
			}

			// op=plugins - every loaded plugin and how many items it provides. This is the
			// enumeration proof: the counts can be checked against the load order on disk.
			if (has("\"plugins\""))
			{
				EnsureBuilt();
				std::string json = std::format(
					"{{\"ok\":true,\"op\":\"plugins\",\"count\":{},\"plugins\":[",
					Catalog::Plugins().size());
				bool first = true;
				for (const auto& p : Catalog::Plugins())
				{
					if (!first) { json += ','; }
					first = false;
					json += std::format("{{\"file\":\"{}\",\"light\":{},\"index\":{},\"items\":{}}}",
										EscapeJson(p.fileName), p.light ? "true" : "false",
										p.index, p.itemCount);
				}
				json += "]}";
				a_write(a_sink, json.c_str());
				return;
			}

			// op=preview:<hex form id> - point the 3D preview at a form with no mouse click.
			// op=pane:cx,cy,size       - move the pane the model is placed inside, in screen fractions.
			// op=place:x,y,z,scale     - the RAW placement override, in the 3D manager's own units,
			//                            which is how the pane-to-model mapping is calibrated; a scale
			//                            of 0 clears the override and hands placement back to the pane.
			// op=previewstate          - what is shown, where the pane is in pixels, and what was last
			//                            handed to the 3D manager.
			//
			// These sat INSIDE the find: branch until 2026-09-09, so they only answered when the
			// arguments also contained "find:" - which is why nothing had ever driven the preview.
			if (const auto at = args.find("preview:"); at != std::string_view::npos)
			{
				const std::string idText(args.substr(at + 8, 8));
				RE::FormID id = 0;
				try { id = static_cast<RE::FormID>(std::stoul(idText, nullptr, 16)); } catch (...) {}
				RE::TESForm* form = id ? RE::TESForm::LookupByID(id) : nullptr;
				UI::SelectForPreview(form);
				a_write(a_sink, std::format(R"({{"ok":{},"op":"preview","formID":"0x{:08X}"}})",
											form ? "true" : "false", id).c_str());
				return;
			}
			if (const auto at = args.find("pane:"); at != std::string_view::npos)
			{
				float cx = 0, cy = 0, size = 0;
				std::sscanf(std::string(args.substr(at + 5, 48)).c_str(), "%f,%f,%f", &cx, &cy, &size);
				preview::SetPane(cx, cy, size);
				float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
				const bool known = preview::GetPaneRect(x0, y0, x1, y1);
				a_write(a_sink, std::format(
					R"({{"ok":true,"op":"pane","cx":{:.3f},"cy":{:.3f},"size":{:.3f},"rectKnown":{},)"
					R"("x0":{:.0f},"y0":{:.0f},"x1":{:.0f},"y1":{:.0f}}})",
					settings::preview::paneX, settings::preview::paneY, settings::preview::paneSize,
					known ? "true" : "false", x0, y0, x1, y1).c_str());
				return;
			}
			if (const auto at = args.find("place:"); at != std::string_view::npos)
			{
				float x = 0, y = 0, z = 0, sc = 0;
				std::sscanf(std::string(args.substr(at + 6, 48)).c_str(), "%f,%f,%f,%f", &x, &y, &z, &sc);
				preview::SetPlacement(x, y, z, sc);
				a_write(a_sink, std::format(R"({{"ok":true,"op":"place","x":{:.1f},"y":{:.1f},"z":{:.1f},"scale":{:.3f}}})",
											x, y, z, sc).c_str());
				return;
			}
			if (has("previewstate"))
			{
				const auto st = preview::GetStatus();
				a_write(a_sink, std::format(
					R"({{"ok":true,"op":"previewstate","available":{},"showing":{},"formID":"0x{:08X}",)"
					R"("loads":{},"frameDrawn":{},"rawOverride":{},)"
					R"("sceneAvailable":{},"attached":{},"attaches":{},)"
					R"("pane":{{"cx":{:.3f},"cy":{:.3f},"size":{:.3f},"x0":{:.0f},"y0":{:.0f},"x1":{:.0f},"y1":{:.0f}}},)"
					R"("applied":{{"x":{:.2f},"y":{:.2f},"z":{:.2f},"scale":{:.3f}}}}})",
					st.available ? "true" : "false", st.showing ? "true" : "false", st.currentFormID,
					st.loads, st.frameDrawn ? "true" : "false", st.rawOverride ? "true" : "false",
					st.sceneAvailable ? "true" : "false", st.attached ? "true" : "false", st.attaches,
					settings::preview::paneX, settings::preview::paneY, settings::preview::paneSize,
					st.paneX0, st.paneY0, st.paneX1, st.paneY1,
					st.posX, st.posY, st.posZ, st.scale).c_str());
				return;
			}

			// op=find:<text> - search every plugin at once, capped so a broad term cannot flood
			// the reply.
			if (has("find:") || has("findall:"))
			{
				EnsureBuilt();
				const std::string needle = After(args, has("findall:") ? "findall:" : "find:");
				bool all[static_cast<std::size_t>(Catalog::Kind::kCount)];
				for (bool& b : all) { b = true; }

				// "find:" hides enchanted variants like the page does; "findall:" includes them.
				const bool withEnchanted = has("findall:");
				// The tool always includes quest items and always sorts A-Z, whatever the page is
				// set to: a driving tool that answered differently depending on a UI setting would
				// make a test's expectations depend on invisible state.
				const auto hits = Catalog::SearchAll(needle, all, withEnchanted, true,
													 Catalog::Sort::kNameAsc, 60);
				std::string json = std::format(
					"{{\"ok\":true,\"op\":\"find\",\"search\":\"{}\",\"returned\":{},\"items\":[",
					EscapeJson(needle), hits.size());
				bool first = true;
				for (const auto* it : hits)
				{
					if (!first) { json += ','; }
					first = false;
					json += std::format(
						"{{\"name\":\"{}\",\"editorID\":\"{}\",\"formID\":\"0x{:08X}\",\"kind\":\"{}\","
						"\"plugin\":\"{}\",\"weight\":{:.1f},\"value\":{}}}",
						EscapeJson(it->name), EscapeJson(it->editorID), it->formID,
						Catalog::KindName(it->kind),
						EscapeJson(it->pluginIndex < Catalog::Plugins().size()
									   ? Catalog::Plugins()[it->pluginIndex].fileName
									   : "?"),
						it->weight, it->value);
				}
				json += "]}";
				a_write(a_sink, json.c_str());
				return;
			}

			// op=give:<formID hex>:<count> - take an item, the same path the page's Add uses.
			if (has("give:"))
			{
				EnsureBuilt();
				const std::string rest = After(args, "give:");
				const auto colon = rest.find(':');
				std::string idText = (colon == std::string::npos) ? rest : rest.substr(0, colon);
				std::uint32_t count = settings::general::defaultCount;

				try
				{
					if (colon != std::string::npos)
					{
						count = static_cast<std::uint32_t>(std::stoul(rest.substr(colon + 1)));
					}
					const auto id = static_cast<RE::FormID>(std::stoul(idText, nullptr, 16));
					RE::TESForm* form = RE::TESForm::LookupByID(id);
					if (!form)
					{
						a_write(a_sink, std::format(
							"{{\"ok\":false,\"op\":\"give\",\"error\":\"no form 0x{:08X}\"}}", id).c_str());
						return;
					}
					Catalog::GiveToPlayer(form, count);
					a_write(a_sink, std::format(
						"{{\"ok\":true,\"op\":\"give\",\"formID\":\"0x{:08X}\",\"count\":{},"
						"\"name\":\"{}\",\"note\":\"queued onto the main thread\"}}",
						id, count, EscapeJson(form->GetName() ? form->GetName() : "")).c_str());
				}
				catch (const std::exception&)
				{
					a_write(a_sink, "{\"ok\":false,\"op\":\"give\",\"error\":\"expected give:<hex formID>[:<count>]\"}");
				}
				return;
			}

			if (has("\"reload\""))
			{
				const bool ok = settings::Reload();
				a_write(a_sink, std::format(
					"{{\"ok\":{},\"op\":\"reload\",\"defaultCount\":{},\"includeSpells\":{}}}",
					ok ? "true" : "false", settings::general::defaultCount,
					settings::general::includeSpells).c_str());
				return;
			}

			a_write(a_sink, std::format(
				"{{\"ok\":true,\"settings\":{{\"defaultCount\":{},\"includeSpells\":{},\"logLevel\":{},"
				"\"iniPath\":\"{}\"}},\"runtime\":{{\"built\":{},\"plugins\":{},\"items\":{}}}}}",
				settings::general::defaultCount, settings::general::includeSpells,
				settings::debug::logLevel, EscapeJson(settings::GetIniPath()),
				Catalog::Built() ? "true" : "false",
				Catalog::Plugins().size(), Catalog::Items().size()).c_str());
		}
	}

	void Init(bool a_lastAttempt)
	{
		static bool registered = false;
		if (registered) { return; }

		DevBenchAPI::IDevBenchInterface001* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench)
		{
			if (a_lastAttempt) { logger::info("DevBench not detected; skipping the \"itemexplorer.control\" tool"); }
			else { logger::debug("DevBench not detected yet; will retry at the next message"); }
			return;
		}

		constexpr const char* descriptor =
			"{"
			"\"description\":\"ApocryphaRealm Item Explorer - every loaded plugin and the items it adds, "
			"read live from TESDataHandler. op=plugins lists each loaded file with its item count (the "
			"enumeration proof - check it against the load order on disk). op=build forces a rebuild. "
			"op=find:<text> searches every plugin at once by item name or editor ID, capped at 60 results, hiding enchanted variants of equipment the way the page does; op=findall:<text> is the same but includes them. "
			"op=give:<hex formID>[:<count>] puts an item in the player's inventory through the game's own "
			"path, queued onto the main thread; a spell is taught instead of added. op=reload re-reads the "
			"INI. No argument reports settings and whether the catalogue has been built yet. "
			"The 3D preview: op=preview:<hex formID> selects the form the preview follows, without a "
			"mouse click; op=pane:<cx>,<cy>,<size> moves the pane it is drawn inside, in fractions of "
			"the screen; op=place:<x>,<y>,<z>,<scale> is the raw placement override in the renderer's "
			"own units, which is how the pane-to-model mapping is calibrated, and a scale of 0 clears "
			"it; op=previewstate reports what is showing, the pane in pixels, and what was last handed "
			"to the game's 3D manager.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"}}},"
			"\"readOnly\":false"
			"}";

		if (devBench->RegisterTool("itemexplorer.control", descriptor, &ControlTool, nullptr))
		{
			logger::info("Registered \"itemexplorer.control\" with DevBench (build {})", devBench->GetBuildNumber());
			registered = true;
		}
	}
}
