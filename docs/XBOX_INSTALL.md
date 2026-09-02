# Xbox 360 installation (XEX-launchable package)

Use `dist/OpenNOW-Xenon-XEX.zip` and extract its **contents directly to the root
of a FAT32 USB drive**. The resulting layout must be:

```text
USB root/
  default.xex
  xenon.elf
  cacert.pem
  README.md
  XellLaunch2-LICENSE.txt
```

In Aurora or XeXMenu, browse to the USB root and launch `default.xex`. That XEX
is the retail XellLaunch2 v2.2.0 loader. It starts embedded XeLL, and XeLL then
automatically discovers and runs `/xenon.elf`. You never need to select an ELF
in the dashboard.

Keep `xenon.elf` and `cacert.pem` at the storage root. XeLL's automatic search
expects the first filename there, while HTTPS authentication and secure
WebSocket signaling require the second. `default.xex` is a launcher for the
bare-metal LibXenon program; changing the ELF's extension to `.xex` would not
produce a valid Xbox executable.

The client initializes wired Ethernet through DHCP and then opens its
controller-driven interface. D-pad selects, A confirms, B cancels, and
BACK+START exits the active stream.

The first console run is the remaining hardware acceptance test. Check that it:

1. obtains and prints a nonzero DHCP address;
2. displays the NVIDIA device-login code and completes login;
3. lists the authenticated game catalog and launches a CloudMatch session;
4. reaches ICE/DTLS connected state and renders video with audio;
5. accepts controller input in the streamed game.

If startup reports a certificate error, confirm that `/cacert.pem` is still at
the USB root. If XeLL starts but OpenNOW does not, confirm that `/xenon.elf` is
at the root and is named exactly in lowercase.

Launcher source and release:

- https://github.com/mitchellwaite/XellLaunch2
- https://github.com/mitchellwaite/XellLaunch2/releases/tag/v2.2.0
