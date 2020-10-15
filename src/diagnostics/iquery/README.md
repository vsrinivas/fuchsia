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

Tests for iquery are available in the `iquery_tests` package:

```
$ fx run-test iquery_tests
```

## Source layout

The entrypoint is located in `main.rs`, with the rest of the implementation
living in top-level `.rs` files. Tests are located in the `testing/` directory.

[inspect]: /docs/development/diagnostics/inspect/README.md
[iquery]: /docs/reference/diagnostics/consumers/iquery.md
