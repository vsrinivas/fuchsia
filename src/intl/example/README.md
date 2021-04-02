# Examples of the localization workflow.

The example will evolve as more automation becomes available.

# Strings

The file `strings.xml` is the source of truth.  This file lists all externalized
strings and gives them `name`s.  The strings will be referenced in the source code
using constants which are generated based on `name`.

You may notice that `strings.xml` has English source text at the moment.  This
is not required: you can have text in any language, including pure gibberish in
there.

# Translated strings

The translated strings are stored in files `strings_<locale_id>.xml`.  For
example, it is expected that the file `strings_fr.xml` contains text which
is a French equivalent of what is written in `strings.xml`.  Since English is
not a special language, `strings_en.xml` must also exist if you want your
program to speak English.

# `BUILD.gn` file

Please refer to the `BUILD.gn` file for a minimal example of how to compile a
C++ program that uses generated strings constants.

# Building and running

## Building

Before you build:

1. Make sure your `fx set` command has `--with=//src/intl/example`.
1. Make sure `fx serve` is running.

```
fx build src/intl/example
```

## Testing

Before you test:

1. Make sure `fx serve` is running.

```
fx test //src/intl/example
```

## Running

**Prerequisite.** You will need to have `fx serve` running.

```
fx shell run \
  fuchsia-pkg://fuchsia.com/src-intl-example#meta/src-intl-example.cmx
```
