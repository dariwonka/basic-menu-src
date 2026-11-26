# Basic Menu (src)

Story-mode menu for GTA VI on a jailbroken PS5.

**Released 26 November 2026. Early work in progress: expect bugs.**

![Menu](images/menu.png)

## Status

- Tested on **PS5 firmware 7.40 only**. Other firmwares are untested.
- Game version **PPSA38500 01.002.001 only**. Any other version is detected and the menu turns its options off.
- Story mode only. The options lock themselves while the console can reach PSN.
- The menu is very buggy at the moment. Options may not work, may stop working, or may crash the game. Save often.

## Features

| Page    | Options                                                                          |
|---------|----------------------------------------------------------------------------------|
| Self    | Heal + armour + repair car, set wanted level (0-5), never wanted, wanted +1 / -1 |
| Vehicle | Spawn any of 254 vehicle models, quick spawns                                    |
| World   | Set hour, freeze time, next weather                                              |
| Info    | Live player / wanted / clock values                                              |

More options are being worked on.

## Controls

| Button       | Action                       |
|--------------|------------------------------|
| R3 + Right   | Open menu                    |
| Up / Down    | Move                         |
| Cross        | Select                       |
| Left / Right | Change value                 |
| Circle       | Back / close                 |

## Using it

Nothing here is a ready-to-run package. You need to:

1. Build the menu from `src/` yourself.
2. Load the built `.prx` into the running game with your own loader.
3. Get every tool, SDK and jailbreak component yourself. None are included, linked or listed here.

The menu writes a log to `/data/BasicMenu/menu.log`.

The prebuilt `.prx` in Releases is provided as-is and needs your own loader.

## Source layout

| File                      | What it does                                        |
|---------------------------|-----------------------------------------------------|
| `main.cpp`                | Module entry, startup, main loop                    |
| `GotHook.*`               | Import-slot hooking                                 |
| `Input.*`                 | Pad hook, button state, game-thread tick            |
| `VideoOut.*`              | Display buffer / flip hooks                         |
| `Overlay.*`               | Draws the menu straight into the display buffer     |
| `Menu.*`                  | Immediate-mode menu framework                       |
| `Features.*`              | Menu pages                                          |
| `Engine.*`                | Game calls, action queue, version and online checks |
| `Offsets.h`               | Addresses for 01.002.001                            |
| `Vehicles.h`              | Vehicle model names and hashes                      |
| `Log.*`, `Notify.*`       | File log, system notifications                      |
| `Font8x8.h`               | Built-in bitmap font                                |

## Credits

- Developed by **XeMo**.
- `Font8x8.h`: public-domain 8x8 font by Daniel Hepper, based on Marcel Sondaar's public-domain VGA font.

## Legal

This is an unofficial fan project. It is **not affiliated with, endorsed, sponsored or approved by Rockstar Games, Take-Two Interactive or Sony Interactive Entertainment**. Grand Theft Auto, GTA, PlayStation and PS5 are trademarks of their respective owners and are used here for identification only.

The repository contains no game code, game files, assets or copyrighted material from Rockstar Games, Take-Two Interactive or Sony. It is for use with a copy of the game you own, in offline story mode only. Do not use it online.

The software is provided "as is", without warranty of any kind. Use it at your own risk. The authors are not responsible for any damage, data loss, bans or other consequences that come from using it.
