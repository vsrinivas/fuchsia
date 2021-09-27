### Start the emulator

If you do not already have an instance running, start FEMU with networking
support:

```posix-terminal
fx vdl start -N --start-package-server
```

When startup is complete, the emulator prints the following message and opens
a shell prompt:

```none {:.devsite-disable-click-to-copy}
To support fx tools on emulator, please run "fx set-device fuchsia-5254-0063-5e7a"
$
```