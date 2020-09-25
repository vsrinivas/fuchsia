# `localized_mod/lib/localization`

Most of the files in this directory were generated. Here's how:

## `intl_messages.arb`

This is generated from `localized_mod/localized_mod_strings.dart` using

```shell
localized_mod$ $FUCHSIA/third_party/dart-pkg/git/flutter/bin/flutter \
packages pub run \
intl_translation:extract_to_arb \
--output-dir=lib/localization \
lib/localized_mod_strings.dart
```

This file should be checked in.

## `intl_messages_*.arb`

These are the result of _manually_ translating `intl_messages.arb` into each
target locale.

One option for generating these more easily is the
[Google Translator Toolkit](https://translate.google.com/toolkit/).

In production code, these would be imported from a translation pipeline.

These files should be checked in.

## `messages_*.dart`

These are generated from the `.arb` files. The command is

```shell
localized_mod$ $FUCHSIA/third_party/dart-pkg/git/flutter/bin/flutter \
packages pub run \
intl_translation:generate_from_arb \
--output-dir=lib/localization \
lib/localized_mod_strings.dart lib/localization/intl_*.arb
```

These are checked into the repo _only as a temporary workaround_.

In the future, there should be a GN build rule that will generate these code
files at build time, so as to not pollute the source tree (fxbug.dev/8750).
