# FIDL test libraries

This directory contains the FIDL test libraries used for golden files.

## Add a new library

To add a new standalone library:

1. Choose a **name**, e.g. `foo_bar`.
2. Create a FIDL file with the `.test.fidl` extension, e.g. `foo_bar.test.fidl`.
3. Declare the library as `fidl.test.` followed by **name** without underscores,
   e.g. `library fidl.test.foobar;`.
4. Add the filename to the `standalone_libraries` list in BUILD.gn.
5. Add an entry to `fidl_testdata_info` in info.gni, providing the **name** and
   the target that BUILD.gn generates, e.g.:

```
{
  name = "foo_bar"
  target = "//zircon/tools/fidl/testdata:fidl.test.foobar"
}
```

To add a new library with dependencies:

1. Choose a **name**, e.g. `foo_bar`.
2. Create a subdirectory named **name** containing two or more FIDL files ending
   in `.test.fidl` and a BUILD.gn to build them.
3. Ensure one of the libraries is named appropriately, e.g. `fidl.test.foobar`,
   and that its build target name is the same.
4. Add an entry to `fidl_testdata_info` in info.gni, providing the **name** and
   the target that BUILD.gn generates, e.g.:

```
{
  name = "foo_bar"
  target = "//zircon/tools/fidl/testdata/foo_bar:fidl.test.foobar"
}
```

## Golden tests

FIDL tools in //tools/fidl use the test libraries as input when defining golden
tests with //build/testing/golden_test.gni. Due to build unification issues, the
fidlc goldens are in //tools/fidl/fidlc/goldens rather than here in zircon.

To run fidlc golden tests:

```
fx test fidlc_golden_tests
```

To regenerate fidlc goldens:

```
fx regen-goldens fidlc
# The above is just a shortcut for this:
fx test fidlgen_golden_tests -- --regen
```
