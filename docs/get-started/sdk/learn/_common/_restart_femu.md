### Restart the emulator

Stop any emulator instances you currently have open:

```posix-terminal
ffx emu stop --all
```

Start a new FEMU instance with networking support:

```posix-terminal
ffx emu start --net tap --headless --name walkthrough-emu workstation.qemu-x64
```
