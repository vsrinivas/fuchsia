# apidiff

The `apidiff` library computes the difference between two FIDL API summaries
produced by the `summarize` library. Its focus is API, but it also 

The main user of this library is `//tools/fidl/fidl_api_diff`.

## Build

```
fx set core.qemu-x64 --with //tools/fidl:tests
fx build tools/fidl/lib/apidiff
```

## Test

```
fx test tools/fidl/lib/apidfiff
```
