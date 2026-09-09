# Changelog

## 1.0.4 - 2026-09-09 - untested

### Changed
- The 3D preview is back in the build, switched on, and it is under test rather than shipped. 1.0.3 held it back because it drew nothing; this version exists to find out which part of how its menu is set up is wrong, and it must go back off before anything is finalized unless a capture shows a model.
- The preview's menu is built from a configuration chosen at runtime instead of a fixed one. `uMenuRecipe` picks the flag set - 0 is InventoryMenu's own, which is what 1.0.3 tried; 1 is the one SKSE's own CustomMenu uses for item display; 2 is that without `kRendersOffscreenTargets`, to isolate that flag; 3 is both families at once. `uLoadMode` picks which overload builds the model. Every combination can now be tried in one game launch instead of one launch per guess.
- The menu can be handed a movie that draws a small marker rectangle instead of the 20-byte blank one, controlled by `bMarkerMovie`. It is an instrument, not a feature: a capture with the marker missing means the game never composited this menu at all, and a capture with the marker present but no model means the compositing works and the model is genuinely absent. Every earlier run could not tell those two apart.
- `itemexplorer.control op=config:<recipe>,<loadMode>,<marker>` sets all three and closes the menu so the next preview rebuilds it, which is what makes the sweep a single launch.

### Why this build exists
- A mod's own menu demonstrably CAN host the game's item 3D. UIExtensions ships `magicmenuext.swf`, a non-vanilla menu whose ActionScript drives `UpdateItem3D`; of every UIExtensions movie installed it is the only one that references it. The vanilla container, inventory, barter and gift movies each carry the same three calls, and magic, favourites and crafting carry none. So this was never a question of whether it is possible.
- One earlier conclusion is corrected here rather than left standing. The SKSE source tree vendored in another of this project's ports declares `Inventory3DManager::UpdateItem3D`, `UpdateMagic3D` and `Clear3D`, which look like engine entry points CommonLibSSE-NG never bound. They are not: they are the same functions CommonLibSSE-NG exposes as the two `LoadInventoryItem` overloads and `UnloadInventoryItem`, under different reverse-engineered names. The two overloads sit +0x30 apart in both trees, in the same order, with the same `InventoryEntryData*` first parameter; the absolute offsets differ only because that tree targets an older Skyrim build. The same trap applies to `IMenu::Render`, which is vtable slot 06 - the slot CommonLibSSE-NG calls `PostDisplay`.

## 1.0.3 - 2026-09-09 - working

### Added
- Favourites. A star beside every item adds it to a second tab of its own, so a short list of things you keep coming back to does not have to be searched for again. The list is kept between sessions and stored as plugin name plus local form ID rather than a raw form ID, so it survives changes to your load order; anything whose plugin is gone is dropped with a line in the log rather than silently pointing at something else.
- Sorting: name A-Z, name Z-A, value highest or lowest first, weight heaviest or lightest first. Ties fall back to the name so the order is stable. The Favourites tab follows the same setting, so switching tabs does not reshuffle everything.
- Quest items are found and marked. Every loaded quest's aliases are walked at catalogue build time and an alias the game itself flags as a quest object marks the form it points at - 288 of them in a vanilla-plus-mods load order. They are tagged [quest] wherever they appear, because taking one can confuse the quest that owns it, and they can be hidden entirely with a switch.

### Fixed
- The DevBench tool's `find` op always includes quest items and always sorts A-Z whatever the page is set to, so a test's expectations never depend on a UI setting.

### Not in this version
- A 3D preview of the selected item was built and is NOT shipped here. The model loads, reaches the game's own UI 3D scene and the scene's render is called, and nothing is drawn - so rather than ship a switch that is on and does nothing, the feature is held back. The work is not lost: it is tagged `3d-preview-research` in the repository, along with what was established (the UI 3D scene render is Address Library id 51855 on 1.5.97; the engine only renders UI 3D while the game is paused; the item menus put their model in `Inventory3DManager`'s own `loadedModels` rather than attaching it to the scene root) and what was ruled out by measurement. It returns when it draws.

## 1.0.2 - 2026-09-08 - working

### Added
- The page is shown in the game's language. Japanese, Korean, Chinese, Russian, German, French, Spanish, Italian, Polish and Czech translation files ship beside the DLL, and the page follows the Apocrypha Menu Framework's Language setting; English is the fallback for anything a file lacks.

### Fixed
- The twelve item-kind names on the page - Weapon, Armor, Ammo, Book, Ingredient, Potion, Scroll, Soul gem, Key, Misc, Light, Spell - were the one piece of visible text that never went through the translation layer, so they would have stayed English in every language. They are translated now. The log lines and the DevBench tool still print the English names on purpose: a tool's identifiers must not change with the player's language.

## 1.0.1 - 2026-09-08 - working

### Added
- Added a switch for enchanted variants, off by default. Skyrim ships hundreds of enchanted versions of every weapon and armour piece, and they bury what a plugin actually adds; with the switch off you see the base equipment only. The DevBench search hides them too, and op=findall includes them.

### Fixed
- The page drew its boolean settings as tick-boxes. They are sliding on/off switches now, as every settings page in this project uses.

## 1.0.0 - 2026-09-08 - working

### Added
- First build. Reads every plugin the game loaded and every item each one adds, straight from the
  game's own data, and shows them on a page: pick a plugin to see what it adds, or search across
  every plugin at once.
- Filter by kind - weapons, armour, ammo, books, ingredients, potions, scrolls, soul gems, keys,
  misc, lights and spells.
- Take any number of an item. It arrives through the game's own path, so it is exactly what a
  container would have handed over; a spell is taught instead.
- The `itemexplorer.control` DevBench tool: `op=plugins` lists every loaded file with its item
  count, `op=find:<text>` searches everything, `op=give:<formID>[:<count>]` takes an item.
- The catalogue is read on demand and kept, not built at startup, so it costs nothing until it is
  wanted.

### Fixed before release
- The page crashed the game the moment it drew on Apocrypha Menu Framework 1.4.9. The framework's
  consumer header calls each ImGui function it resolves without checking whether the resolve
  succeeded, so a call the loaded framework does not export is a jump to address zero. The page now
  uses only calls present in the oldest supported framework - plain rows with an Add button instead
  of a table, and it refuses to register itself at all, with a log line, if anything it needs is
  missing.
- The plugin list came out as 3,640 entries for a load order of a few hundred, because the loaded-mod
  accessors read through version-dependent offsets. Plugins are now discovered from the forms
  themselves, which needs no offsets and is right on every runtime.

### Proven in game
- 482 plugins and 35,424 items read from a full load order in about 1.5 seconds; search returns
  correct names, kinds, plugins and form IDs; taking an item puts it in the player's inventory.
