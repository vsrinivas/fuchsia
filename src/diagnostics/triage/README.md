# Triage (fx triage)

Reviewed on: 2019-12-08

Triage analyzes snapshots for out-of-range values. The analysis is specified by
configuration files.

## Building

To add this project to your build, append `--with //src/diagnostics/triage` in
the `fx set` invocation.

For example:

```
fx set core.chromebook-x64 --with '//topaz/bundles:buildbot' --with //src/diagnostics/triage
```

## Running

To fetch and analyze a fresh snapshot from the default device:

```
fx triage
```

To analyze an existing snapshot:

```
fx triage --data /path/to/snapshot.zip
```

or

```
fx triage --data /path/to/unzipped_snapshot_dir
```

To specify config files to use:

```
fx triage --config path/file1 --config path/file2 --config "path/with/globs/*.triage"
```

Note that the triage command, not the OS, must expand the globs, so put the path
in quotes.

TODO(cphoenix): Should these paths be relative to tree-root, or CWD?

Config file format is described in [Configuring 'fx triage'](config.md). It
includes:

*   Selectors which specify the data to extract from the inspect.json produced
    by snapshot.zip.
*   Eval expressions which specify calculations.
*   Actions to take on specified values.
*   Tests to ensure your actions trigger (or not) appropriately with sample data
    you supply.

## Testing

Use `--with //src/diagnostics/triage:tests` for all tests.

Unit tests for the Rust code are linked into CQ/CI. There are also a series of
integration tests which live in the integration.rs file which are run in CQ/CI
as well. To run them manually:

```
fx test triage_lib_tests
```

## Source layout

*   //tools/devshell/contrib/triage - A bash script which implements the entry
    point to the "fx triage" command.
    *   Invokes //src/diagnostics/triage/src/main.rs to analyze a snapshot.
*   //src/diagnostics/triage/
    *   Entry point of the main "triage" program. Parses command line arguments,
        loads files, launches analysis, and formats output.
*   //src/diagnostics/lib/triage/src/
    *   metrics.rs - Data structures and calculation engine for metrics.
        *   metrics/fetch.rs - Business logic to read values from the
            inspect.json file.
    *   act.rs - Data structures and business logic to store and carry out
        actions.
    *   validate.rs - The data structures and business logic to store and
        evaluate the self-tests specified in the config files.
    *   config.rs - Loads configuration information from *.triage files.
        *   config/parse.rs - A `nom`-based parser for Eval metrics.
*   //src/diagnostics/triage/test_data/ - Data which is used in integration
    testing.
*   //src/diagnostics/triage/build
    *   triage_config_test.gni defines a gn target to run config tests in CQ.
    *   triage_config_test_runner defines a binary which executes the tests for
        the triage_config_test.gni target.
