# Test data for zxdb

This is used for the symbol parsing tests.

There are two flavors of tests. Some build the symbol test and run on that, expecting to find
certain symbol patterns.

Tests that rely heaving on the binary layout use the checked-in version to avoid compiler and
platform variations.

## To generate the binaries

  * On x64 copy the generated `zxdb_symbol_test` that includes symbols to
    `libsymbol_test_so.targetso`. As of this writing, the compiled file will be something
    like: `out/x64/host_x64/test_data/zxdb/libzxdb_symbol_test.targetso`

  * Copy that file to `libsymbol_test_so_stripped.targetso`

  * Run `strip libsymbol_test_so_stripped.targetso`

