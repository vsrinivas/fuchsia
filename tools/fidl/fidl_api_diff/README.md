# fidl_api_diff

The program `fidl_api_diff` computes the difference between two FIDL API
surfaces as described by the [FIDL API summary JSON format][jsonf].

## Set


The first set, `//tools`, is needed to ensure that the testing utilities are
present.

```
fx set core.qemu-x64 --auto-dir \
   --with=//tools \
   --with=//tools/fidl
```

## Compile

```
fx build tools/fidl/fidl_api_diff
```

## Test

```
fx test tools/fidl/fidl_api_diff
```

## Example use

The following script demonstrates the use of the program for diffing an API
of an in-tree FIDL library.

```
#!/bin/bash
readonly _build_dir="$(fx get-build-dir)"
mkdir -p "${HOME}/tmp"
fx build tools/fidl
"${_build_dir}/host_x64/fidl_api_summarize" \
  --before-file "$HOME/tmp/before.api_summarize.json" \
  --after-file "$HOME/tmp/after.api_summarize.json" \
  --api-diff-file "$HOME/tmp/result.api_diff.json" \
  "${@}"
```

<!-- xrefs -->

 [jsonf]: /tools/fidl/fidl_api_summarize/README.md
