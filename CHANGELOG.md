# Changelog

## 1.0.4 - 2026-09-10 - working

### Changed
- The 3D item preview is removed. Its settings toggle, the pane position and size sliders, the pane-marking toggle, the per-frame work and the row click that fed it are all gone, as are the DevBench operations that existed only to drive it (config, preview, pane, place, previewstate, scheme). The preview movies and every preview key in the INI go with it, so the shipped configuration matches the code.
- Quantity is now a slider rather than a typed number, and its ceiling follows the item. Gold has its own slider up to 10000, on the gold row itself, because its sensible range is nothing like anything else's. Consumables and crafting materials go up to 50. Everything else stays at 1, because fifty cuirasses is never what was meant.
- Crafting materials count as consumables for that purpose. They cannot be told apart by item kind - ingots, ore, leather and strips are all Misc records - so they are recognised by the vanilla vendor keywords VendorItemOreIngot, VendorItemAnimalHide, VendorItemFirewood and VendorItemGem, read off the form rather than guessed from its name.

### Why the preview was removed
- It never drew, across several sessions of investigation. The player's `bShowInventory3D=0` was found to have been off the whole time, which looked like the answer - but with it on, the preview still renders nothing. Driven from DevBench, everything on this mod's side succeeds: the model loads, its fade is forced to 1, it attaches, the manager reports one model, a frame is drawn, and the game's own UI 3D scene reports the mesh present with the camera and eight lights. Forcing the raw placement to the camera origin still produced no image. The pane itself draws correctly - its corner marks and the item's name appear - and is simply empty.
- So the feature is out until that is understood, rather than shipped switched on and doing nothing. The reasoning failure that cost the earlier sessions is recorded as entry 54 in the logic library: prove the host feature works without you before assuming your own code is at fault.

### Version note
- The number 1.0.4 was previously stamped on an untested investigation build that put the preview back in deliberately. That build was never released, and under the project's versioning rule an untested build does not consume its number, so 1.0.4 is reused here for the first working version after 1.0.3.

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
