# Ermine internationalization and localization

This Dart package contains the support for Ermine Shell localization and
internationalization.

At its current state the support is in its very early phase.  There is a lot of
manual scaffolding that could be replaced by auto-generating the code of
interest.  Doing so will be the topic of followup work.

# How to use this package

This package is purpose-built for the Ermine Shell.  The central part is the 
file `lib/strings.dart` which by design contains all the strings that the Ermine
Shell needs to vary based on the system locale.

Add a function to this file whenever you need to introduce a new text message.

# Adding a dependency

To make the library available to other Ermine Shell packages, add a dependency:

```
"//src/experiences/session_shells/ermine/internationalization"
```

to the dependencies in the appropriate `BUILD.gn` rule of your package.

# Importing the new dependency

To import the new dependency, add the import:

```dart
import 'packages:internationalization/strings.dart';
```

to the import section of any Dart file that needs strings.

# Referring to localized strings

You can now refer to the messages defined in [strings.dart][lib/strings.dart].
For example, you can refer to the string `Ask` by calling `Strings.ask()`.
This is done so that the localization system can substitute the word `Ask` for
the appropriate word in the language defined by the current locale.  The approach
extends in similar ways for most strings; with slight variations depending on 
whether number or date, or plural or gender formatting is needed.

# Generating localizable libraries

To generate the ARB files, please refer to the instructions in
`fuchsia_internationalization`.

An example invocation is below, assuming that `$FUCHSIA_DIR` is set to the full
path of the directory containing the Fuchsia checkout.

```
$FUCHSIA_DIR/topaz/public/dart/fuchsia_internationalization_flutter/scripts/run_extract_to_arb.sh
```

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

An example invocation is below.

```
$FUCHSIA_DIR/topaz/public/dart/fuchsia_internationalization_flutter/scripts/run_generate_from_arb.sh
```

# Further reading

Internationalization and localization are topic unto themselves, and it is out
of scope of this file to go into all the details.  Please see the section
on [Internationalizing Flutter apps][1] for many more details that are directly
relevant to Dart and Flutter app localization.

[1]: https://flutter.dev/docs/development/accessibility-and-localization/internationalization 
[arb]: https://github.com/google/app-resource-bundle 
