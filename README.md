# Wrench Throw

Throw the OmniWrench as in *Ratchet & Clank Future: A Crack in Time*, in OpenRAC's PC port of Ratchet & Clank.

**Videos:** [the throw](https://youtu.be/0yEa0jzs4_4) · [throwing with a weapon in hand](https://youtu.be/40figBwoU80)

| On foot | |
|---|---|
| **Hold R2** | With the wrench in hand, Ratchet raises it, ready to throw |
| **R2 + Square** | Throws it where he faces, standing, running or jumping. He keeps moving. With a weapon in hand, he switches to the wrench and throws it at once |
| **Square alone** | The normal combo |

## Good to know

- R2 no longer crouches on foot. **R1 still does**, so the high jump and the crouching swing are still there.
- On the hoverboard (Rilgar, Kalebo III) the mod leaves every button to the game, so R2 and the other triggers do tricks as usual.
- With [Lock-strafe](https://github.com/OpenRAC/openrac-mods/tree/master/lock-strafe), set its **Strafe button** to **L2 only**: L2 strafes and R2 stays for the wrench.
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

## Changes

- **1.0.3**: hoverboard tricks work again. The game treats the hoverboard as being on foot, so the mod hid R2 there too and the tricks (and Rilgar's trick skill point) could not be done. The mod now leaves the buttons alone in the hoverboard's control mode. Thanks to EspeonScottie for the report.
- **1.0.2**: R2 + Square with a weapon or gadget in hand switches to the wrench and throws it.
- **1.0.1**: works beside Lock-strafe (strafe on L2 only).
- **1.0.0**: first release.
