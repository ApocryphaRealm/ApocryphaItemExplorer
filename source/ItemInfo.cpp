#include "PCH.h"

#include "ItemInfo.h"

#include "utils/Logger.h"

#include <atomic>
#include <cctype>
#include <cmath>
#include <mutex>

namespace iteminfo
{
	namespace
	{
		std::mutex             g_lock;
		Info                   g_info;                 // the last finished read
		std::uint32_t          g_wanted = 0;           // what the page asked for last
		std::atomic<bool>      g_queued{ false };      // a read is waiting in the task queue

		// A book's text is the page markup the game's book menu reads: <font>, <p>, <img>, [pagebreak]. The card shows
		// the words only, and only the start of a long book - the rest is what the book menu is for.
		constexpr std::size_t kBookChars = 500;   // short enough that the inventory details below still show

		std::string StripMarkup(const std::string& a_in)
		{
			std::string out;
			out.reserve(a_in.size());
			bool inTag = false;
			for (std::size_t i = 0; i < a_in.size(); ++i)
			{
				const char c = a_in[i];
				if (inTag) { if (c == '>') { inTag = false; } continue; }
				if (c == '<')
				{
					// A book's first letter is often a picture (an illuminated capital, <img src='...Illuminated_Letters/V_...'>):
					// put the letter back, or the text starts "ircine's Tale" (first in-game run, 2026-10-23).
					const std::size_t end = a_in.find('>', i);
					const std::string tag = a_in.substr(i, end == std::string::npos ? std::string::npos : end - i);
					if (const std::size_t at = tag.find("Illuminated_Letters/"); at != std::string::npos)
					{
						const std::size_t l = at + 20;
						if (l + 1 < tag.size() && std::isalpha(static_cast<unsigned char>(tag[l])) && tag[l + 1] == '_') { out += tag[l]; }
					}
					inTag = true;
					continue;
				}
				if (c == '[' && a_in.compare(i, 11, "[pagebreak]") == 0) { out += "\n"; i += 10; continue; }
				if (c == '\r') { continue; }
				out += c;
			}
			// collapse runs of blank lines and spaces left where tags were
			std::string tidy;
			tidy.reserve(out.size());
			int newlines = 0;
			for (char c : out)
			{
				if (c == '\n') { if (++newlines <= 2 && !tidy.empty()) { tidy += c; } continue; }
				if (c == ' ' && (tidy.empty() || tidy.back() == ' ' || tidy.back() == '\n')) { continue; }
				newlines = 0;
				tidy += c;
			}
			while (!tidy.empty() && (tidy.back() == '\n' || tidy.back() == ' ')) { tidy.pop_back(); }
			return tidy;
		}

		void ReplaceAll(std::string& a_s, std::string_view a_what, const std::string& a_with)
		{
			for (std::size_t at = a_s.find(a_what); at != std::string::npos; at = a_s.find(a_what, at + a_with.size()))
			{
				a_s.replace(at, a_what.size(), a_with);
			}
		}

		std::string Number(float a_v)
		{
			const float r = std::round(a_v);
			if (std::fabs(a_v - r) < 0.05F) { return std::to_string(static_cast<long long>(r)); }
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%.1f", a_v);
			return buf;
		}

		// The effect's own sentence with the figures put in, the way the inventory writes it.
		void AddEffects(const RE::MagicItem* a_magic, std::vector<std::string>& a_out)
		{
			if (!a_magic) { return; }
			for (const RE::Effect* effect : a_magic->effects)
			{
				if (!effect || !effect->baseEffect) { continue; }
				const char* raw = effect->baseEffect->magicItemDescription.c_str();
				std::string line = raw ? raw : "";
				// an effect with no description is left out, as the game's own item card does (an enchantment's hidden
				// second effect read "Fire Damage" on its own line - first in-game run, 2026-10-23)
				if (line.empty()) { continue; }
				ReplaceAll(line, "<mag>", Number(effect->GetMagnitude()));
				ReplaceAll(line, "<dur>", std::to_string(effect->GetDuration()));
				ReplaceAll(line, "<area>", std::to_string(effect->GetArea()));
				a_out.push_back(StripMarkup(line));
			}
		}

		std::string Description(RE::TESForm* a_form, std::uint32_t a_field = 'CSED')
		{
			auto* desc = a_form ? a_form->As<RE::TESDescription>() : nullptr;
			if (!desc) { return {}; }
			RE::BSString text;
			desc->GetDescription(text, a_form, a_field);
			return text.c_str() ? std::string(text.c_str()) : std::string();
		}

		// Main thread: everything the card shows, read off the form.
		Info Read(RE::TESForm* a_form)
		{
			Info info;
			info.formID = a_form ? a_form->GetFormID() : 0;
			info.ready = true;
			if (!a_form) { return info; }

			switch (a_form->GetFormType())
			{
			case RE::FormType::Weapon:
				if (auto* w = a_form->As<RE::TESObjectWEAP>())
				{
					info.damage = static_cast<int>(w->GetAttackDamage());
					info.speed = w->GetSpeed();
					info.reach = w->GetReach();
					info.critDamage = static_cast<int>(w->GetCritDamage());
					info.description = StripMarkup(Description(a_form));
					AddEffects(w->formEnchanting, info.effects);
				}
				break;
			case RE::FormType::Armor:
				if (auto* a = a_form->As<RE::TESObjectARMO>())
				{
					info.armorRating = a->GetArmorRating();
					switch (a->GetArmorType())
					{
					case RE::BGSBipedObjectForm::ArmorType::kLightArmor: info.armorClass = ArmorClass::kLight; break;
					case RE::BGSBipedObjectForm::ArmorType::kHeavyArmor: info.armorClass = ArmorClass::kHeavy; break;
					default:                                             info.armorClass = ArmorClass::kClothing; break;
					}
					info.description = StripMarkup(Description(a_form));
					AddEffects(a->formEnchanting, info.effects);
				}
				break;
			case RE::FormType::Ammo:
				if (auto* am = a_form->As<RE::TESAmmo>())
				{
					auto& rt = am->GetRuntimeData();
					info.damage = static_cast<int>(std::round(rt.data.damage));
					const char* s = rt.shortDesc.c_str();
					info.description = s ? StripMarkup(s) : std::string();
				}
				break;
			case RE::FormType::Book:
				if (auto* b = a_form->As<RE::TESObjectBOOK>())
				{
					if (b->TeachesSpell())
					{
						if (auto* spell = b->GetSpell())
						{
							const char* n = spell->GetFullName();
							info.teachesSpell = n ? n : "";
							AddEffects(spell, info.effects);
						}
					}
					else if (b->TeachesSkill())
					{
						if (auto* avl = RE::ActorValueList::GetSingleton())
						{
							// the array itself: the lookup call is spelled differently on the 1.5.97 and 1.7 CommonLib lines
							const auto idx = static_cast<std::size_t>(std::to_underlying(b->GetSkill()));
							if (auto* av = idx < std::size(avl->actorValues) ? avl->actorValues[idx] : nullptr)
							{
								const char* n = av->GetFullName();
								info.teachesSkill = n ? n : "";
							}
						}
					}
					// the inventory's one-line description first (CNAM), else the start of the book itself
					RE::BSString card;
					b->itemCardDescription.GetDescription(card, a_form, 'MANC');
					std::string text = card.c_str() ? StripMarkup(card.c_str()) : std::string();
					if (text.empty())
					{
						text = StripMarkup(Description(a_form));
						if (text.size() > kBookChars)
						{
							std::size_t cut = text.rfind(' ', kBookChars);
							text = text.substr(0, cut == std::string::npos ? kBookChars : cut) + " ...";
						}
					}
					info.description = std::move(text);
				}
				break;
			case RE::FormType::SoulGem:
				if (auto* g = a_form->As<RE::TESSoulGem>())
				{
					info.soulCapacity = static_cast<int>(g->GetMaximumCapacity());
					info.soulHeld = static_cast<int>(g->GetContainedSoul());
				}
				break;
			case RE::FormType::AlchemyItem:
			case RE::FormType::Ingredient:
			case RE::FormType::Scroll:
			case RE::FormType::Spell:
				AddEffects(a_form->As<RE::MagicItem>(), info.effects);
				if (a_form->GetFormType() == RE::FormType::Spell || a_form->GetFormType() == RE::FormType::Scroll)
				{
					info.description = StripMarkup(Description(a_form));
				}
				break;
			default:
				info.description = StripMarkup(Description(a_form));
				break;
			}
			logger::debug("iteminfo: {:08X} - description {} chars, {} effect lines", info.formID, info.description.size(), info.effects.size());
			return info;
		}

		void QueueRead()
		{
			if (g_queued.exchange(true)) { return; }   // one read in the queue at a time; it picks up the latest ask
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) { g_queued = false; return; }
			tasks->AddTask([]() {
				std::uint32_t id = 0;
				{
					std::scoped_lock l(g_lock);
					id = g_wanted;
				}
				// looked up again here, on the main thread: the page's pointer may belong to a rebuilt catalogue
				Info info = Read(id ? RE::TESForm::LookupByID(id) : nullptr);
				info.formID = id;
				{
					std::scoped_lock l(g_lock);
					g_info = std::move(info);
				}
				g_queued = false;
				// the page moved on while this read ran: read the newer one
				bool again = false;
				{
					std::scoped_lock l(g_lock);
					again = g_wanted != g_info.formID;
				}
				if (again) { QueueRead(); }
			});
		}
	}

	void Request(RE::TESForm* a_form)
	{
		const std::uint32_t id = a_form ? a_form->GetFormID() : 0;
		{
			std::scoped_lock l(g_lock);
			if (id == g_wanted && (g_info.formID == id || g_queued.load())) { return; }
			g_wanted = id;
		}
		QueueRead();
	}

	Info Current()
	{
		std::scoped_lock l(g_lock);
		Info copy = g_info;
		if (copy.formID != g_wanted) { copy.ready = false; }
		return copy;
	}
}
