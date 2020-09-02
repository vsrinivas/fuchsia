# Test cases for symbolizer

This directory contains the test cases for Fuchsia symbolizer. Each test case has an input file
ending with ".in" and an output file ending with ".out". Check the beginning of each file for a
description for the test case.

## Download symbol files

To run a symbolizer against a test case, first make sure the symbol files are downloaded at
`//prebuilt/test_data/symbolizer/symbols`. This could be done by
`jiri init -fetch-optional=symbolizer-test-data && jiri fetch-packages`.

## Run the test cases manually

Currently the test cases are not hooked in any automated workflow. Here's an example to run the Go
symbolizer against `cpp_crasher_syslog.in` manually.

```
fx symbol-index add prebuilt/test_data/symbolizer/symbols
fx symbolize < tools/symbolizer/test_cases/cpp_crasher_syslog.in
```

## Upload symbol files for new test cases

When new test cases are added, normally new symbol files need to be added to
`//prebuilt/test_data/symbolizer/symbols`. This could be done by `copy_symbols.py`.
It will copy all mentioned symbol files to `//prebuilt/test_data/symbolizer/symbols`.
Then you can upload it using `cipd`.

```
cipd create -install-mode copy \
  -name fuchsia/test_data/symbolizer/symbols \
  -in prebuilt/test_data/symbolizer/symbols \
  -tag version:$(date +%Y%m%d)
```
