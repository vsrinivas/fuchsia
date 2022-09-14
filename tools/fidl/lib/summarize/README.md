# summarize

The `summarize` library extracts FIDL API information from FIDL JSON IR.

The main user of this library is `//tools/fidl/fidl_api_summarize`.

## Build

```
fx set core.qemu-x64 --with //tools/fidl:tests
fx build tools/fidl/lib/summarize
```

## Test

```
fx test tools/fidl/lib/summarize
```
