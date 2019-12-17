# Triage (fx triage)

Reviewed on: 2019-12-08

Triage analyzes bugreports for out-of-range values. The analysis is specified by
configuration files.

## Building

This project can be added to builds by including `--with //src/diagnostics/triage`
in the `fx set` invocation.

For example:

```
fx set core.chromebook-x64 --with '//topaz/bundles:buildbot' --with //src/diagnostics/triage
```

## Running

To fetch and analyze a fresh bugreport from the default device:

```
fx triage
```

To analyze an existing bugreport:

```
fx triage --inspect /path/to/bugreport.zip
```

To specify config files to use:

```
fx triage --config path/file1 --config path/file2 --config "path/with/globs/*.triage"
```

Note that the triage command, not the OS, must expand the globs, so put the
path in quotes.

TODO(cphoenix): Should these paths be relative to tree-root, or CWD?

Config file format is described in [Configuring 'fx triage'](config.md). Briefly, you specify:

 *   Metrics
     *   Selector metrics specify the data to extract from the inspect.json produced
         by bugreport.zip
     *   Eval metrics specify calculations
 *   Actions to take if a condition is true (currently, only "print a warning")
 *   Tests to ensure your actions trigger (or not) appropriately with sample data
     you supply.

## Testing

Unit tests for the Rust code are linked into CQ/CI. To run them manually:
```
fx run-test triage_tests
```

There's a manually launched integration test to test the bash script:
```
fx build && fx triage --test
```

## Source layout

*   //tools/devshell/contrib/triage - A bash script which implements the entry
    point to the "fx triage" command.
    *   By default, it invokes //src/diagnostics/triage/src/main.rs
        to analyze a bugreport.
    *   With the `--test` argument, it dispatches
        an integration test by invoking //src/diagnostics/triage/
        test/src/main.rs.
*   //src/diagnostics/triage/src/
    *   main.rs - Entry point of the main "triage" program. It reads command
        line arguments, loads the config files and inspect.json file that they
        specify, and then processes the actions and self-tests specified in
        the config files.
    *   metrics.rs - Data structures and calculation engine for metrics.
        *   metrics/fetch.rs - Business logic to read values from the
            inspect.json file.
    *   act.rs - Data structures and business logic to store and carry out
        actions.
    *   validate.rs - The data structures and business logic to store and
        evaluate the self-tests specified in the config files.
    *   config.rs - Loads configuration information from *.triage files.
        *   config/parse.rs - A `nom`-based parser for Metrics that calculate
            values.
*   //src/diagnostics/triage/test/src/main.rs - Integration tester that invokes
    the "fx triage" script and evaluates its output.
