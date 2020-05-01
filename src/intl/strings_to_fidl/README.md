# FIDL generation for intl

Contains a program that generates FIDL constants file from [Android
strings.xml][android-strings] file.  The generated file contains the message
IDs of all messages present in the file.

[android-strings]: https://developer.android.com/guide/topics/resources/string-resource

## Example use

The following example takes input from the file `strings.xml` and writes the
output into `strings.xml.fidl`.

```
./strings_to_fidl \
  --input=strings.xml \
  --output=strings.xml.fidl
```

## Running tests

```
fx test //src/intl/strings_to_fidl
```

