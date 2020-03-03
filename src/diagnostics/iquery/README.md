# iquery

Reviewed on: 2019-07-22

iquery is a command line tool for discovering and retrieving [inspect][inspect]
data exposed by components. More can be read about iquery [here][iquery].

## Building

To add this project to your build, append `--with //src/diagnostics/iquery`
to the `fx set` invocation.

## Running

Documentation on using iquery is available [here][iquery].

## Testing

Tests for iquery are available in the `iquery_golden_test` package:

```
$ fx run-test iquery_golden_test
```

## Source layout

The entrypoint is located in `main.rs`, with the rest of the implementation
living in top-level `.rs` files. Tests are located in the `testing/` directory.

[inspect]: /docs/development/inspect/README.md
[iquery]: /docs/development/inspect/iquery.md
