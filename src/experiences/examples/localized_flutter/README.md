# Localized Mod

This is an example of a simple Flutter application that can display localized
strings in several supported locales. It uses
[`dart:intl_translation`](https://pub.dartlang.org/packages/intl_translation)
for extracting localized strings from code and for generating the
localization-loading classes.

This is intended as a testbed for any efforts to integrate Fuchsia's Gerrit with
a translation pipeline.

## Building

    fx set workstation.chromebook-x64 --with //src/experiences/examples/localized_flutter
    fx build

## Testing

To run unit tests

    fx set workstation.<BOARD> --with //src/experiences:tests
    fx run-host-tests localized_flutter_app_unittests
