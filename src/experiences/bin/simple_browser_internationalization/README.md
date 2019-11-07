# `simple_browser` internationalization and localization

This Dart package contains the support for `simple_browser` localization and
internationalization.

At its current state the support is in its very early phase.  There is a lot of
manual scaffolding that could be replaced by auto-generating the code of
interest.  Doing so will be the topic of followup work.

# How to use this package

This package is purpose-built for the `simple_browser`.  The central part is
the file `lib/strings.dart` which by design contains all the strings that the
simple browser needs to vary based on the system locale.

Add a function to this file whenever you need to introduce a new text message.

## Translating

The translation process amounts to making ARB files for each of the supported
locales of interest and providing the translations for the messages and
strings.  The basic tool for doing so would be a simple text editor. While
there also exists a wealth of tools that help software translators, it is out
of scope of this file to go into specific details.  So the choice of the
translation environment is left to the developer and translator.

## Generating Dart runtime format for the translations

Once translated you can use the file `./scripts/run_generate_from_arb.sh` from
`fuchsia_internationalization` library to generate the Dart code that wires up
the translation.  You will need to rebuild the entire project to take the new
translations in.

# Further reading

Internationalization and localization are topic unto themselves, and it is out
of scope of this file to go into all the details.  Please see the section
on [Internationalizing Flutter apps][1] for many more details that are directly
relevant to Dart and Flutter app localization.

[1]: https://flutter.dev/docs/development/accessibility-and-localization/internationalization
[arb]: https://github.com/google/app-resource-bundle
