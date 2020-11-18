# dlog

Prints the kernel's debuglog to stdout. Primarily used in zedboot configurations.

## Usages

`dlog` is launched by the [virtcon in zedboot], see `SetupVirtconEtc`.

The [botanist] tool also uses this tool to print the contents of the debuglog for infra usage.

[virtcon in zedboot]: /src/bringup/bin/console-launcher/virtcon-setup.cc
[botanist]: /tools/botanist/cmd/zedboot.go
