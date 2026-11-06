# FNFWii

A Wii port of [Friday Night Funkin'](https://github.com/FunkinCrew/Funkin), written in C on libogc.

It reads the game's own data (characters, stages, weeks, charts, note styles) and mirrors its
rules for timing, scoring, health, camera and character placement, so songs play the way they do
on the computer.

* Story mode, freeplay (with difficulties and the Erect/Pico mixes), options
* Every week from Tutorial to Weekend 1, including the pixel week
* Adobe Animate characters (BF, GF, Pico, Darnell...) baked into frames ahead of time
* Wii Remote (sideways), Wii Remote + Nunchuk, Classic Controller, GameCube controller
  (also through the Wii U adapter), Wii U Pro Controller and USB keyboards, through
  [libwiiinput](https://github.com/quatric/libwiiinput)
* Update notices and one-button updates through [libwiiupdate](https://github.com/quatric/libwiiupdate)
* 16:9 (anamorphic) or 4:3, following the console setting

## Controls

| | Sideways Wii Remote | Classic / GameCube / Pro | Keyboard |
|---|---|---|---|
| Notes | D-pad, or B A 1 2 | D-pad, or the face buttons | Arrows or W A S D |
| Confirm | 2 or A | A | Enter / Space |
| Back | 1 | B | Esc / Backspace |
| Pause | + | + / Start | Enter / Esc |
| Reset (die) | − | − | R |

## Building

You need devkitPPC and libogc, plus the `ppc-libvorbisidec` and `ppc-libogg` portlibs.

```sh
git clone --recursive https://github.com/quatric/FNFWii.git
cd FNFWii
make
```

### Game data

Release builds come pre-packaged with all game assets.

If you are building from source, download and convert the assets from the
[Funkin assets repository](https://github.com/FunkinCrew/funkin.assets) (needs Python 3 and Pillow):

```sh
git clone --depth 1 https://github.com/FunkinCrew/funkin.assets.git
python3 tools/convert.py funkin.assets data_out
make dist
```

`make dist` puts everything in `dist/apps/FNFWii` (about 800 MB with every song). Copy the
`apps` folder to the root of an SD card or USB drive.

## Testing in Dolphin

`scripts/autotest.sh` boots the game in Dolphin with a scripted `autotest.txt` (scenes, button
presses, seeks, screenshots) and saves the screenshots as PNGs. The scripts in `test/` cover the
menus, a full song, story mode and game over:

```sh
make dist
scripts/autotest.sh test/out/bopeebo test/bopeebo.txt
```

## Credits

Friday Night Funkin' is by The Funkin' Crew Inc. This port is not affiliated with them.
