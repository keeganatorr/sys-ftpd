# sys-nxlink

Port of nxlink from nx-hbmenu to a sysmodule.
Uses a custom nx-hbloader to boot the linked .nro

(THIS IS A BETA/POC USE WITH KNOWLEDGE)
only works for the first session so far

`https://github.com/jakibaki/sys-ftpd`
`https://github.com/roblabla/nx-hbloader`
`https://github.com/switchbrew/nx-hbmenu`

## Instructions

Send .nro homebrew to swith with nxlink
```nxlink -a <switch ip> <nro file> -s```

and boot the homebrew menu though launching the album viewer, and nxlink should link up to the sent homebrew.

## Issues

Only works for one session
Does not auto boot homebrew
nxlink ping support (currently you must send to the switch's IP directly)

## Compiling

The build script provided produces binaries for sys-nxlink and the custom nx-hbloader.

```./build.sh```

Copy `main` and `main.npdm` to `sdmc:/atmosphere/titles/010000000000100D/exefs/`
Copy `sys-nxlink.kip` to `sdmc:/modules/`
Edit `hekate_ipl.ini` and add
```
[NXLINK + CFW]
kip1=modules/sys-nxlink.kip
kip1=modules/newfirm/loader.kip
kip1=modules/newfirm/sm.kip
```
as a new section


## Todo (after release)

Fix looping
hold L for normal boot
Find out how to autoboot the homebrew
only boot if server active