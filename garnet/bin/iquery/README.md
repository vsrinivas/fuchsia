# iquery

Reviewed on: 2019-07-22

iquery is a command line tool for discovering and retrieving [inspect][inspect]
data exposed by components. More can be read about iquery [here][iquery].

## Building

This project can be added to builds by including `--with //src/sys/…` to the `fx
set` invocation.

## Running

Documentation on using iquery is available [here][iquery].

## Testing

Tests for iquery are available in the `iquery_golden_tests` package:

```
$ fx run-test iquery_golden_test
```

## Source layout

The entrypoint is located in `main.cc`, with the rest of the implementation
living in top-level `.cc` and `.h` files. Tests are located in the `testing/`
directory.

[inspect]: /docs/development/inspect/README.md
[iquery]: /docs/development/inspect/iquery.md
