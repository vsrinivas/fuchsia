# JSON generation for intl

`strings_to_json` is a program that takes a `strings.xml` file for a source language,
a compatible `strings.xml` file for a target language, and produces a Fuchsia 
localized resource -- which for the time being is using a semi-formal JSON format
for message encoding.

You should normally not need to invoke the program directly. It will be made
available through gn build rules.

## Example use

```
./strings_to_json --source-locale=en-US --target-locale=fr-FR \
    --source-strings-file=strings.xml \
    --target-strings-file=strings_fr.xml \
    --output=fr.json
```

## Building

The program is built as a host tool only.  The command line below will build
and install the program into the directory
`$FUCHSIA_DIR/out/your_build_directory/host-tools`.

```
fx build //src/intl/strings_to_json:install
```

## Running tests

```
fx test //src/intl/strings_to_json
```

