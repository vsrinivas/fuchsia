# fuchsia_inspect_flutter

Reviewed on: 2019-07-29 by charlescha@

fuchsia_inspect_flutter is a library used for exposing Inspect data when
debugging Flutter Applications on Fuchsia.

## Building

This project can be added to builds by including `--with
//topaz/packages/tests:dart_unittests` to the `fx set` invocation.

## Using

fuchsia_inspect_flutter can be used by depending on the
`//src/sys/lib/fuchsia_inspect_flutter’
GN target.

## Testing

Unit tests for library are available in
`//topaz/public/dart/fuchsia_inspect_flutter/test`
directory. The tests can be run using

```
$ fx run-host-tests inspect_flutter_test
```

## Source layout

The main implementation is in `lib/src/inspect_flutter.dart`, which also
has unit tests in `test/`.

