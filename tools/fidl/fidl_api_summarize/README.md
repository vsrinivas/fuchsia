# fidl_api_summarize

The program `fidl_api_summarize` extracts FIDL API information from FIDL JSON
IR. It wraps the `//tools/fidl/lib/summarize` library.

## Build

```
fx set core.qemu-x64 --with //tools/fidl
fx build host-tools/fidl_api_summarize
```

## Example use

```
fx fidl_api_summarize \
    --output-file /dev/stdout \
    --fidl-ir-file "$(fx get-build-dir)/fidling/gen/sdk/fidl/fuchsia.net/fuchsia.net.fidl.json"
```
