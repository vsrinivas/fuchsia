# `hwstress`

Tool for exercising hardware components, such as CPU and RAM, to try and detect
faulty hardware.

Example usage:

```sh
hwstress
```

See also:

*  `loadgen` (`src/zircon/bin/loadgen`) which has many threads performing
   cycles of idle / CPU work, useful for exercising the kernel's scheduler.

*  `kstress` (`src/zircon/bin/kstress`) which is designed to stress test
   the kernel itself.
