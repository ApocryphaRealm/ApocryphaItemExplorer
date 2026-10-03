# Changelog

## 1.1.3 - 2026-10-03 - untested

### Changed
- The log ships at info (uLogLevel=2, compiled and in the INI), the standing default since 2026-09-26; it shipped at trace. 1.1.2 (the amount sliders) was tagged but not released; 1.1.3 is the release carrying it.

## 1.1.2 - 2026-10-03 - untested

### Added
- A quantity slider on every row you hold a pile of - potions and food, ingredients, scrolls, arrows and bolts, soul gems, torches, lockpicks, and crafting materials (ore and ingots, hides and leather, firewood, gems, claws, feathers, dragon bone and scale, Hearthfire's building materials) - on the Browse page AND the Favourites page. Each row keeps its own amount (1 to 50) for the session; the one "How many" slider at the top of Browse is gone. The owner, 2026-10-02.

### Changed
- Every slider moves one unit of its shown digit per keyboard or D-pad nudge (rule 68): the amount sliders, gold, and the preview's position and size. A mouse drag and a typed number are unchanged.

### Fixed
- Firewood never counted as a pile: the game spells its keyword VendorItemFireword, and only the correct spelling was checked. Crafting materials are now also recognised by use - any Misc item a crafting recipe takes (218 in the test load order, Hearthfire's clay, nails, hinges, logs and glass among them) - instead of by keyword alone.

## 1.1.1 - 2026-09-19 - untested

### Fixed
- **The whole row answers the mouse.** The owner, 2026-09-19: *"Whenever I use the left click of the mouse to select
  an item to preview in the preview pane, it doesn't do anything."* Most of a row is plain text, and text is not a
  control, so a click on an item's NAME was claimed by nothing and reached the framework's window, which started
  dragging it. The row's rectangle is now tested against the cursor at the end of each row: hovering anywhere in it
  previews the item, clicking anywhere in it fixes the item. It adds no stop for the controller's highlight to walk
  through, so controller navigation is unchanged. (Apocrypha Menu Framework 1.9.6 is the other half of this: its
  window now moves only by its top bar, which is what was eating the click.)
- **The preview pane was empty and the item was drawn in the inventory's own preview spot instead.** The owner,
  2026-09-19: *"The items don't appear in our preview window at all and are using the preview window that's in the
  inventory itself."* The previous attempt bound our own render target and let the engine paint into it. That cannot
  work: `Inventory3DManager::Render` sets up its own targets through BSGraphics before it draws, so the redirect was
  simply overridden - the model went to the live frame at its usual place, and our texture stayed blank.

  The capture is back to the way Modex does it, and the way this mod did it before: save the frame, blank it, let the
  engine paint, lift the result into our texture, put the saved frame back. The player's frame ends byte-identical to
  the one that would have been drawn.

  The one change from the older version is the rectangle: it is now the WHOLE SCREEN rather than a computed box.
  Every leak this preview has had came from a box that did not cover what the model painted - it shrank with zoom,
  then it was clamped to a 2048-pixel texture while the model kept painting past the clamp. A full-frame copy cannot
  be too small. It costs three screen-sized copies on the frames where a capture actually happens, which is a handful
  after each change of item or turn of the model, not every frame.
- A frame whose size or format does not match the capture textures now rebuilds them and says so in the log, instead
  of a copy being silently refused and the pane staying empty.

### Fixed
- **A second copy of the model appearing on screen, and a smear left behind while turning it.** The owner, 2026-09-19,
  spinning a weapon zoomed right in: *"a second preview appears in the bottom middle of the screen that's also
  rotating"*, *"spinning the weapon would cause the texture to sort of leave a, a line of texture like a pencil on
  paper"*, and his own reading, which was correct: *"it seems like there's two areas inside the preview pane and When
  the item intersects both planes, it tears the texture."*

  The preview does not render the model anywhere private - it lets the engine paint it onto the live frame and works
  a rectangle around it: save the pixels under the rectangle, clear it, let the engine paint, lift the rectangle into
  a texture, put the saved pixels back. Zoom was implemented by SHRINKING that rectangle, so that a smaller piece of
  the screen was lifted into the same pane. Zoom in far enough and the rectangle becomes smaller than the area the
  engine actually paints, and everything outside it is never captured, never cleared and never restored - it is simply
  left on the live frame. The two "planes" were the model's real painted extent and the capture rectangle.

  The rectangle now depends only on the pane, so it always contains what is painted, and **zoom is applied to the
  model's own scale instead** - a bigger model fills more of a rectangle that still holds it. Nothing can fall outside
  the rectangle any more, because the rectangle is no longer what zoom moves.

  Researched before it was rebuilt, and worth recording: the capture pipeline came from Modex originally, but Modex's
  current source contains no `Inventory3DManager` at all - every one of its 63 source files was checked. The reference
  was always its issue #48, a discussion, not shipped code, so there was nothing left to copy and the fix comes from
  the diagnosis instead.

### Added
- **The 3D preview box takes the mouse** (the owner, 2026-09-19: *"you hold left mouse button while on the item
  and it rotates 360 and theres no need to move its position in the frame but i want the scroll wheel to zoom in
  and out"*). Hold the LEFT mouse button on the item and drag to turn it - across turns it, up and down tilts it,
  and the turn never wraps or stops, so it goes round as many times as you keep dragging. The WHEEL zooms in and
  out. A turn that started in the box keeps the mouse until the button comes up, so the model does not stop
  turning when the pointer leaves the frame. The turn is applied to the model before the capture rectangle is
  measured, so the box still frames it correctly at any angle, and a new item always starts upright. Zoom changes
  how much of the screen is lifted into the box rather than re-rendering at another size, so it costs nothing.
- **The controller takes hold of the item with R3** (the owner, 2026-09-19: *"controller should have press r3 while
  on the list item to activate the preview pane and start manipulating the item with left and right stick and circle
  to exit and go back to the list item"*). Press R3 on a highlighted row and the preview box takes the sticks: the
  LEFT stick turns the item, the RIGHT stick zooms, and neither moves the selection, so the highlight is still on
  that row when CIRCLE gives the sticks back. Turn and zoom speeds are per second rather than per frame, so they
  feel the same whatever the frame rate is doing. Needs Apocrypha Menu Framework 1.9.5, which is where the sticks
  come from; on an older framework the calls are absent, R3 does nothing and the mouse half is unaffected.
- **The box's shipped position and size are now the owner's own** - `fPaneX = 0.87`, `fPaneY = 0.64`,
  `fPaneSize = 0.44` - in the compiled defaults and in the shipped INI together (rule 16).

### Performance
- **The page no longer costs most of the frame rate while it is open.** phbd01 (2026-09-19, Nexus):
  *"every time I install it and open the Item Explorer, I get a huge FPS drop - around 80%. This has never
  happened with any other add-item mods I've used."* Four things were being redone on every single frame the
  page drew, none of which could have changed since the frame before:
  - **The sort lower-cased both names inside its comparator** - two heap allocations per comparison, so about
    2*N*log(N) of them per sort. Selecting Skyrim.esm put roughly twenty thousand items through that every frame.
  - **The filters lower-cased every item's name and editor ID** while walking the catalogue - about 52,000
    string allocations per frame across a 26,000-item load order.
  - **"Is this a stack item?" walked each visible row's keyword list**, comparing four keyword strings, per row,
    per frame.
  - **The 3D preview re-rendered the model and copied the capture rectangle three times per frame** (save the
    world pixels, lift the model, put the world back) plus a full-rect clear - tens of megabytes of GPU copies
    a frame at 4K, for a picture identical to the last one.

  Names and the stack-item answer are now worked out once when the catalogue is read; both list queries are
  memoised against their inputs and recomputed only when the search text, the filters, the sort or the selected
  plugin actually change; and the preview captures only when the picture would differ - a different item, pane
  size, model scale or background - with a two-second budget so a slow model load still ends in an image.
  Nothing about what the page shows changed.

## 1.1.0 - 2026-09-18 - untested

### Fixed
- **The Address Library guard now runs before SKSE::Init.** CommonLibSSE-NG's Init opens the Address Library itself, so the guard added for a missing file sat after the very call that fails on it and never ran; oproso's log (Perfected Wheeler 1.3.2, 2026-09-18) showed the banner, then CommonLib's bare 'failed to open address library file', and no [AddressLibrary] line. The check is now the first thing after the logger, so a missing file is named - game version, file, folder - and the plugin loads inert.

## 1.0.9 - 2026-09-18 - working

### Fixed
- **Searching "gold" finds gold even when a mod has renamed it** (the owner, 2026-09-18: *"its called septims"*). SE keeps no editor id in memory for most forms, so a renamed engine item had nothing but its new name to match; Gold001, Lockpick and SkeletonKey now carry their editor ids in the catalogue.
- **An engine-defined form with no source file is listed under Skyrim.esm instead of being skipped.** Looked into after the owner could not find Gold at first (2026-09-18: *"i cant find gold in item explorer after searching every plugin"* - he then found it, so Gold itself was never dropped); the catalogue used to skip any form whose GetFile(0) is null, which is the case for some forms the engine creates before any plugin is read (ids below 0x800). They are Skyrim.esm's.

### Removed
- **The "Frame the box and show the item's name" toggle** (the owner, 2026-09-18: *"we dont need a toggle for the frame visibility"*). The preview box is always framed with the menu's theme frame and always carries the item's name; the bShowFrame INI key and its translation key are gone.

### Changed
- **The Kinds filter toggles sit in three neat columns** (the owner, 2026-09-18: *"i want the filter toggles to be in neat columns of 3 toggles instead of them all being aliigned to the left"*). A table lays them out, so the columns line up whatever each label's width is; before, a chain of SameLine calls put four ragged toggles on a row and the skipped spell toggle threw its count off.

## 1.0.8 - 2026-09-18 - untested

### Changed
- The preview box's frame is the Apocrypha Menu Framework's own theme frame - the Skyrim theme's Nordic knotwork, drawn just outside the box the way the framework frames its window - through the framework's new `AMF_DrawThemeFrame` export (1.8.9). On an older framework, or a theme without a frame, the box keeps its plain white line.
- The item's name is centred across the top of the box.

## 1.0.7 - 2026-09-18 - working

### Added
- The 3D item preview is back, and this time it draws. The item under the cursor - or under D-pad focus - appears in a floating box: black, white-framed, the item's name inside it, drawn in front of the framework window wherever you put it (three sliders on the Browse page: across, down, size; `[Preview]` in the INI keeps them). The model is the game's own inventory render, lifted off the engine's frame buffer into a texture the page draws opaque on the black. What was wrong before (1.0.3-1.0.4): the engine only builds inventory 3D for a menu that pauses the game the inventory's way (kPausesGame with kDisablePauseMenu), the load only completes inside the engine's own render call, that render draws into the frame buffer the engine has bound rather than the swap chain, and on SE 1.5.97 CommonLibSSE-NG's Render binding points at a three-argument sibling - so the inventory's real render (Address Library 50882) is called by id. The flag recipe and the capture come from Modex - Mod Explorer Menu (Patchuli; issue #48 and its Item3DPreview), credited in THIRD_PARTY_NOTICES.md. While the box is showing the game is paused, as it is inside the inventory. The engine's own `bShowInventory3D` switch is turned on for the session if a player's INI has it off, and restored after.
- DevBench: `op=preview:<hex formID>` points the box at a form, `op=previewstate` reports the engine-side state (model held, captures landed, geometry), `op=pane:<cx>,<cy>,<size>` moves the box, `op=norestore:1` leaves the engine's paint on screen for a diagnostic frame.

### Removed
- The 1.0.4-era preview research: the menu-recipe, camera, light-scheme, load-mode and marker-movie settings and the DevBench xrefs/datarefs/fx/dump/scanmenu/menuflags/uiscene/openmenu operations. `[Preview]` in the INI is a smaller section: position, size, frame, model scale, offset, background.

## 1.0.6 - 2026-09-16 - untested

### Changed
- The DLL, its INI and its log lose the Apocrypha prefix - ItemExplorer.dll, ItemExplorer.ini, ItemExplorer.log. Your existing ApocryphaItemExplorer.ini is no longer read, so settings return to defaults; copy your values across if you had changed any.

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
