# Fuchsia internationalization and localization

This Dart package contains the support for Fuchsia localization and
internationalization.

At its current state the support is in its very early phase.  There is a lot of
manual scaffolding that could be replaced by auto-generating the code of
interest.  Doing so will be the topic of followup work.

# Adding a dependency

To make the library available to other packages, add a dependency:

```
//sdk/dart/fuchsia_internationalization_flutter"
```

to the dependencies in the appropriate `BUILD.gn` rule of your package.

# Importing the new dependency

To import the new dependency, add the import:

```dart
import 'package:fuchsia_internationalization_flutter/internationalization.dart';
```

to the import section of any Dart file that needs strings.

# Referring to localized strings

You can now refer to the messages defined in [strings.dart][lib/strings.dart].
For example, you can refer to the string `Ask` by calling `strings.ask()`.
This is done so that the localization system can substitute the word `Ask` for
the appropriate word in the language defined by the current locale.  The approach
extends in similar ways for most strings; with slight variations depending on
whether number or date, or plural or gender formatting is needed.

# Generating localizable libraries

Two scripts are provided in the `./scripts` directory.  One is
`./scripts/run_extract_to_arb.sh`, which produces a "translation interchange
form" in the form of [ARB][arb] files. The ARB files can be translated
independently of the source code, and should be checked into the code base for
now.

## Translating

The translation process amounts to making ARB files for each of the supported
locales of interest and providing the translations for the messages and
strings.  The basic tool for doing so would be a simple text editor. While
there also exists a wealth of tools that help software translators, it is out
of scope of this file to go into specific details.  So the choice of the
translation environment is left to the developer and translator.

## Generating Dart runtime format for the translations

Once translated you can use the file `./scripts/run_generate_from_arb.sh` to
generate the Dart code that wires up the translation.  You will need to rebuild
the entire project to take the new translations in.

# Further reading

Internationalization and localization are topic unto themselves, and it is out
of scope of this file to go into all the details.  Please see the section
on [Internationalizing Flutter apps][1] for many more details that are directly
relevant to Dart and Flutter app localization.

[1]: https://flutter.dev/docs/development/accessibility-and-localization/internationalization
[arb]: https://github.com/google/app-resource-bundle
