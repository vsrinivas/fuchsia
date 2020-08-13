# cmc: the component manifest compiler

Reviewed on: 2019-07-11

cmc provides validation and manipulation logic for component manifest files.
This tool is largely intended to be used by build rules, and not directly
invoked by humans. Information on how to use it is available by running the tool
with `--help`.

cmc is purely a host tool, and is not built for nor available on fuchsia
systems.

## Building

This project is already included in host builds and should be included in any `fx set`.

## Running

cmc is available at `$OUT_DIR/host_x64/cmc` in the build output path after an `fx build`
invocation.

```
$ ./out/default/host_x64/cmc --help
```

## Testing

Make sure the tests are added to your build by adding
`--with //tools/cmc:tests` to your `fx set` invocation.

Unit tests for cmc are available in the `cmc_bin_tests` binary, which can be
invoked with the `fx run-host-tests` command:

```
$ fx run-host-tests --names cmc_bin_test
```

Integration tests are also available in the `cmc_integration_test` package.

```
$ fx run-test cmc_integration_test
```

## Source layout

The entrypoint is located in `src/main.rs`, unit tests are co-located with the
code, and the integration tests live in `tests/`.
