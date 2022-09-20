# fidl_api_diff

The program `fidl_api_diff` computes the difference between two FIDL API
summaries produced by `fidl_api_summarize`. Its focus is API, but it also has
limited support for detecting breaking ABI changes. It wraps the
`//tools/fidl/lib/apidiff` library.

## Build

```
fx set core.qemu-x64 --with //tools/fidl
fx build host-tools/fidl_api_diff
```

## Example use

```
summarize() {
    echo "$1" > test.fidl
    $(fx get-build-dir)/host_x64/fidlc --json test.json --files test.fidl
    fx fidl_api_summarize --output-file /dev/stdout --fidl-ir-file test.json
}

summarize "library l; type T = table {};" > before.json
summarize "library l; type T = table { 1: b bool; };" > after.json

fx fidl_api_diff --api-diff-file /dev/stdout \
    --before-file before.json --after-file after.json
```
