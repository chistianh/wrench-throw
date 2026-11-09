# Wrench Throw

Throw the OmniWrench as in *Ratchet & Clank Future: A Crack in Time*, in OpenRAC's PC port of Ratchet & Clank.

**[Watch the video](https://youtu.be/0yEa0jzs4_4)**

| With the wrench in hand | |
|---|---|
| **Hold R2** | Ratchet raises the wrench, ready to throw |
| **R2 + Square** | Throws it where he faces, standing, running or jumping. He keeps moving |
| **Square alone** | The normal combo |

## Good to know

- R2 no longer crouches while you hold the wrench. **R1 still does**, so the high jump and the crouching swing are still there.
- With a weapon in hand, switch to the wrench first.
- It does not work with Lock-strafe: both use R2.
- Tested on the PAL disc. **NTSC-U is not tested yet**: please report how it goes.

## How it works

The throw is the game's own: the function it uses from the first-person view, so damage and the wrench's return are unchanged. The arm is posed with joint offsets on top of whichever animation is playing. No game files are included.

## Install

In the launcher's **Mods** tab, choose **Add from a link** and paste this repository's address. You need your own copy of the game.

## Build

```sh
python3 tools/fetch_sdk.py
cmake -S . -B build -G Ninja
cmake --build build
```

Made with AI assistance. License: GPL-3.0-or-later. The hero and pad addresses come from OpenRAC's [Lock-strafe](https://github.com/OpenRAC/openrac-mods/tree/master/lock-strafe).
