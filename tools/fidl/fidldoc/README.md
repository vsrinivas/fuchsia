Fidldoc is a command-line tool that generates reference pages based on FIDL JSON IR.

# Build
1. Include fidldoc in your build, e.g. `fx set core.x64 --with //tools/fidl/fidldoc`
1. Build with `./buildtools/ninja -C out/default tools/fidl/fidldoc:fidldoc`

# Run
1. Run with a single file `fidldoc fuchsia-device.fidl.json`
1. Run with a list of files `fidldoc json/*`

By default, docs will be generated in `./docs/`. You can change that by specifying the `--output` (or `-o`) command-line flag.
Use the `--template` (or `-t`) flag to select the output format. Currently, the two supported values are `markdown` and `html`.

Additional configurations can be provided in a `fidldoc.config.json` file and passed as part of the command
line interface.

Example:

```
./out/default/host_x64/fidldoc -v \
    ./out/x64/fidling/gen/zircon/public/fidl/fuchsia-mem/fuchsia-mem.fidl.json \
    -c ./out/default/host_x64/fidldoc.config.json
```

The `all_fidl_json.txt` file provides a list of file locations to all of the FIDL JSON IR files that
are part of the Fuchsia build. You can use this file to generate all of the reference docs.
The snippet below shows an example of how to do it:

```
FIDL_PATHS=$(cat ./out/default/all_fidl_json.txt | sed -e 's/^/.\/out\/default\//')
./out/default/host_x64/fidldoc -v $(echo $FIDL_PATHS)
```

You can optionally pass a `--tag` flag to specify a source definition.

Example - How to use the `--tag` flag to point to the current git commit or branch:

```
./out/default/host_x64/fidldoc -v \
    ./out/x64/fidling/gen/zircon/public/fidl/fuchsia-mem/fuchsia-mem.fidl.json \
    -c ./out/default/host_x64/fidldoc.config.json \
    --tag $(git rev-parse HEAD)
```

# Test
The `tools/fidl/fidldoc:fidldoc` target will automatically generate a unit test binary in the build output directory.
Run the tests with `./out/default/host_x64/fidldoc_bin_test`.

Alternatively, you can run the tests with `fx run-host-tests fidldoc_bin_test`.