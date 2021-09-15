# clang_doc_filter

This program converts a directory of YAML reports produced by `clang_doc` into
a report usable by test coverage.  This tool is only built for the host
toolchain.

## Compile

```
fx set ... \
    --with=//sdk/cts/plasa/clang_doc_filter:tests \
    --with=//sdk/cts/plasa/clang_doc_filter:host
```

## Test

```
fx test //sdk/cts/plasa/clang_doc_filter
```

## Invocation

The program supports a number of flags. (Please refresh this list if it goes
out of date. In case of discrepancies, the tool output is always more current.)

```
╰─>$ ./out/workstation.qemu-x64/host_x64/clang_doc_filter --help
Usage of ./out/workstation.qemu-x64/host_x64/clang_doc_filter:
  -allow-filename-regexp value
        a regexp that may match any part of the filename
  -allow-symbol-name-regexp value
        a regexp that may match any part of the symbol name
  -input-dir string
        the input directory to get the files from
  -output-file string
        the file to write the final report to
```

### Flags

* `-allow-filename-regexp`: filter the report down to only the files that match
    the supplied regexp.  You can specify more than a single instance of this
    flag, and any match will work.  If unset, no filtering happens.
* `-allow-symbol-name-regexp`: filter the report down to only the symbol names
    that match the supplied regexes.  You can specify multiple instances of
    this flag, and any match will work. If unset, no filtering happens.  If it
    is specified together with the above flag, *either* match will work.
* `-input-dir`: the top level output directory with the `clang_doc` YAML report
    files.  This tool should be compatible with the `clang_doc` format of the
    prebuilt compiler in `//prebuilts/...`.  **REQUIRED**.
* `-output-file`: the file name to write the JSON output to.  **REQUIRED**.

### Input

The input format is a set of YAML files placed in a directory subtree beneath
the directory denoted as `-input-dir`. This input format matches the YAML output
format of the [`clang_doc`][cd] tool.  This data format seems to be informally
specified, so we make a best effort of decoding what data is given to us by
the `clang_doc` from the prebuilt package that we use to build Fuchsia at any
given time.

[cd]: https://clang.llvm.org/extra/clang-doc.html

If you are curious about the sample output of `clang_doc`, take a peek into the
[`testdata`][td] directory.  This directory contains some sample report bits
from the output which we use in testing. Please note that these reports are
usually huge, so we cut down on the amount of detail to the extent that they
are important for the tests of `clang_doc_filter`.

The data model for the report has been manually extracted and can be inspected
at [model.go][modgo]. There is no process that would keep the model in
continuous sync with what `clang_doc` uses. We will see whether that becomes
necessary in the future. For now, we count on being able to make pragmatic
updates when those are necessary.

[td]: testdata/
[modgo]: model.go


### Output

The output is written out to the output file specified by the flag
`-output-file`.  The output format is a simple JSON; the data model for the
report is in [report.go][rgo].  Please refer to [report_test.go][rtgo] for
examples of the text output.

[rgo]: report.go
[rgo]: report_test.go




