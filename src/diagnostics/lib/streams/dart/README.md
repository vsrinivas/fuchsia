# Dart Diagnostic Streams

Diagnostics Streams is an experimental library for writing logs in the
[log encoding][encoding]. The library will eventually be merged into the
`fuchsia_logger` library in //topaz/public/dart and should not be used directly.

## Testing

Unit tests for Diagnostic Streams are available in the
`fuchsia-diagnostic-streams-unittests` package. To run these tests, include
`--with //bundles:tests` in the `fx set` invocation and run the following command:

```
$ fx test fuchsia-diagnostic-streams-unittests
```

## Source layout

Public members of the library are exposed in `lib/streams.dart`. Private
implementations are in `lib/src/*`. Unit tests may be found in `test/*`.

[encoding]: /docs/reference/diagnostics/logs/encoding.md
