# DIFL

DIFL is a tool to evaluate changes between different versions of FIDL libraries.
It describes the changes in terms of their impact on compatibility

## Usage

*TODO*

## Testing

In the [Fuchsia tree](https://fuchsia.googlesource.com/) the easiest way to test
is by invoking a test script via:
```
fx build garnet/public/lib/fidl/tools/difl_test_fidl
```

As well as running tests this will generate some test FIDL libraries. Then you
can run the regular difl tool like:
```
scripts/difl --before \
  out/default/gen/garnet/public/lib/fidl/tools/difl_test_fidl/before.fidl.json \
   --after \
   out/default/gen/garnet/public/lib/fidl/tools/difl_test_fidl/after.fidl.json
```
