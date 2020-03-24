The programs in this directory show how to use the Spinel API
to render various images. Most of them expect a input SVG file to
be passed on the command-line.

# Building:

Running `fx build src/graphics/lib/compute:all_tests` will rebuild
both host and Fuchsia binaries at the same time.

# Running on the host:

Invoke directly after building, as in:

```sh
out/default/host_x64/<program-name> <input-svg>
```

# Running on a Fuchsia device:

The input document needs to be copied into a program-specific directory
on the device before running the program, as in:

```sh
PROGRAM=spinel-svg-demo
fx cp --to-target <input-file> /data/cache/r/sys/fuchsia.com:${PROGRAM}:0#meta:{PROGRAM}.cmx/
fx shell run $PROGRAM /cache/<input-file>
```

