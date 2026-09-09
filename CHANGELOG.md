# Changelog

## 1.0.3 - 2026-09-09 - untested

### Added
- Favourites. A star beside every item adds it to a second tab of its own, so a short list of things you keep coming back to does not have to be searched for again. The list is kept between sessions and stored as plugin name plus local form ID rather than a raw form ID, so it survives changes to your load order; anything whose plugin is gone is dropped with a line in the log rather than silently pointing at something else.
- Sorting: name A-Z, name Z-A, value highest or lowest first, weight heaviest or lightest first. Ties fall back to the name so the order is stable. The Favourites tab follows the same setting, so switching tabs does not reshuffle everything.
- Quest items are found and marked. Every loaded quest's aliases are walked at catalogue build time and an alias the game itself flags as a quest object marks the form it points at - 288 of them in a vanilla-plus-mods load order. They are tagged [quest] wherever they appear, because taking one can confuse the quest that owns it, and they can be hidden entirely with a switch.
- A 3D preview of the selected item, drawn by the game's own inventory renderer - the same one that shows an item when you highlight it in your inventory. Click a row to select it. One item at a time, because that is how that renderer works.
- The preview has a PANE you can put where you like, marked with corner brackets and the item's name above it. It has to be movable, and it defaults to the right of the screen, because the model is drawn by the game earlier in the frame than this page is drawn over it: wherever the two overlap the page wins and the model is behind it. Nothing is painted inside the brackets for the same reason - a backing plate there would hide what it frames. The brackets and the caption use the framework's screen-wide drawing (Apocrypha Menu Framework 1.5.8 and later), the same mechanism SkyHUD Settings Menu marks HUD positions with.
- Pane controls on the page - across, down, size, and a switch for the brackets - and the pane, the mapping from it to the renderer's own units, and the model's placement are all kept in the INI, so where you put it survives a restart.

### Fixed
- The DevBench tool could not drive the 3D preview at all. Its `preview:` and `place:` ops had been written INSIDE the `find:` branch, so they only answered when the arguments also contained `find:` - which is how the preview reached a release without anyone having seen it draw. They are top-level ops now, joined by `pane:` and `previewstate`.

### Changed
- The DevBench tool's find op always includes quest items and always sorts A-Z whatever the page is set to, so a test's expectations never depend on a UI setting.

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
