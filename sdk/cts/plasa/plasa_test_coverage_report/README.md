# `plasa_test_coverage_report`

The program `plasa_test_coverage_report` extracts the subset of the
Fuchsia platform surface area which is useful in test coverage.

## Building


```
cd $FUCHSIA_DIR
fx set ... --args='generate_plasa_artifacts=true' --with=//sdk/cts/plasa
fx build sdk/cts/plasa/plasa_test_coverage_report
```

## Testing

```
cd $FUCHSIA_DIR
fx set ... --args='generate_plasa_artifacts=true' --with=//sdk/cts/plasa
fx test //sdk/cts/plasa/plasa_test_coverage_report
```

## Running

You would not normally run this program as a command line tool
directly.  However, this may sometimes be convenient for inspection
or debugging.

An example session using the program directly is given here.

```
cd $FUCHSIA_DIR
fx build sdk/cts/plasa/plasa_test_coverage_report
cd $(fx get-build-dir)
fx plasa_test_coverage_report --plasa-manifest-file=plasa.manifest.json
```

### Producing the report via the build system

This may be more convenient to do if you are interested in making changes to
the program or are interested in using the program output as input to a
different process (such as test coverage reports).

An example output examination is shown below. Note that the `fx set` command
needs to be complete to refer to your build directory and any other packages
and options you may want to add.

```bash
fx set ... --args='generate_plasa_artifacts=true' --with=//sdk/cts/plasa
fx build sdk/cts/plasa:api_coverage_report
cat $(fx get-build-dir)/test_coverage_report.plasa.json
```

## Example output

The following is an excerpt of the output. The output conforms to
the [data schema][sch].

[sch]: schema/test_coverage_report.schema.json

```
{
    "items": [
        {
            "name": "::FidlCodedArray::FidlCodedArray",
            "kind": "api_cc"
        },
        {
            "name": "::FidlCodedBits::FidlCodedBits",
            "kind": "api_cc"
        },
        {
            "name": "fuchsia.library/Protocol.member",
            "kind": "api_fidl"
        },
        {
            "name": "::FidlCodedEnum::FidlCodedEnum",
            "kind": "api_cc"
        },
        ...
    ]
}
```
