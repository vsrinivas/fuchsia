# fidl_api_summarize

The program `fidl_api_summarize` extracts FIDL API information from the [FIDL
intermediate representation][fidlir] files.

## Set

The first set, `//tools`, is needed to ensure that the testing utilities are
present.  Otherwise, `fx test` may not work.  The second set, `//src/lib/intl` is
only used in the "Example use" below.
```
fx set core.qemu-x64 --auto-dir --with=//tools
```
## Compile
```
fx build tools/fidl/fidl_api_summarize
```
## Test
```
fx test tools/fidl/fidl_api_summarize
```
## Invoke

Build the tool first. Once built, the following command line in bash runs it.
```
fx fidl_api_summarize --help
```
## Example use

The following script demonstrates the use of the program for summarizing an API
of an in-tree FIDL library.  It assumes that a FIDL IR file (`fidl.fidl.json`)
is already existing.  This should already be the case if you used the `fx set`
command from the "Set" section above verbatim, or if you adjusted your existing
set of packages to include `//src/lib/intl`.

```
#!/bin/bash
readonly _build_dir="$(fx get-build-dir)"
mkdir -p "${HOME}/tmp"
fx build tools/fidl
"${_build_dir}/host_x64/fidl_api_summarize" \
  --output-file "$HOME/tmp/intl.api" \
  --fidl-ir-file \
    "${_build_dir}/fidling/gen/src/lib/intl/intl_property_provider_impl/fidl.fidl.json" \
  "${@}"
```
<!-- xref -->

[fidlir]: /docs/reference/fidl/language/json-ir

