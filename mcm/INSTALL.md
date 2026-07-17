# Embody MCM front-end — install & test

This folder is the **optional** in-game config menu for Embody. The core mod (the DLL + INI) works without any
of this. The menu just gives you a friendly UI over the same `Data\MCM\Settings\Embody.ini` the plugin reads.

## What's here

```
mcm/Data/
├── Embody.esp                              ← ESL-flagged plugin: one quest that registers the menu
└── MCM/Config/Embody/
    ├── config.json                         ← the 3-page menu layout
    └── settings.ini                        ← default values (MCM Helper seeds Settings\Embody.ini from this)
```

## Requirements (for the menu only)

- **SkyUI** (the MCM system)
- **MCM Helper** (Exit-9B) — reads `config.json`, provides the `MCM_ConfigBase` script the quest uses
- **Embody.esp** — the tiny quest in this folder (enable it in your load order; it's ESL-flagged, no slot cost)

The core plugin still only needs SKSE64 + Address Library. Skip all of the above and you still get the mod + the
live-reloading INI; you just hand-edit `Data\MCM\Settings\Embody.ini` instead of clicking a menu.

## Install

1. Install **SkyUI** and **MCM Helper** normally.
2. Merge this folder's `Data/` into your `Data/` (MO2: pack `mcm/` as a mod). That places `Embody.esp` and the
   `MCM/Config/Embody/` files.
3. Enable **Embody.esp** in your load order.
4. Launch through SKSE. Open **System → Mod Configuration → Embody**.

## The two pages

- **Body Tuning** — anchor mode, body scale, and the 12 per-stance offset sliders. This is what you'll use.
- **Controls** — rebind every hotkey. The tuning hotkeys are **OFF by default** (so they can't clash with other
  mods); flip **Enable Tuning Hotkeys** on to use them. The 10 keybinds grey out while it's off.

## How it shares data with the plugin

MCM Helper writes your choices to `Data\MCM\Settings\Embody.ini` with type-prefixed keys (`iMode`, `bHotkeysEnabled`,
`iStandSheathedFwd`…). The Embody plugin reads that exact file and hot-reloads it (~1×/sec), so menu changes apply
live without a relaunch. Keymap controls store DirectInput scan codes; the plugin bridges them to virtual-keys
internally. One file, one source of truth — the menu and the plugin never disagree.
