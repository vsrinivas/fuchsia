# FIDL linter nterface

This document describes the command-line interface to the FIDL linter.

For more information about FIDL's overall purpose, goals, and requirements,
see [Overview](../intro/README.md).

## Overview

The FIDL linter is a command line program that processes one or more FIDL
files, and prints warnings about content that compiles (technically valid FIDL),
but appears to violate rules from the [FIDL Style Rubric][fidl-style].
Readability is important, and style is a component of that, but the FIDL Rubric
also defines rules that help ensure the FIDL API does not include things that are
known to hamper cross-language portability.

## Use `fx lint`

Fuchsia includes the `fx lint` command that automatically selects and runs the
appropriate code linter for each of a set of specified files. `fx lint` bundles
the files with a `.fidl` extension, and passes all of them, together, to the FIDL
linter command `fidl-lint`.

`fx lint` is the recommended way to invoke the FIDL linter, and ideally should be
run before uploading new FIDL librarys or changes to existing FIDL. Without any
arguments, `fx lint` will run all available linters on all files in your most
recent `git commit`.

```sh
fx lint
```

To review other available options, run:

```sh
fx lint --help
```

<!-- xrefs -->
[fidl-style]: /docs/development/languages/fidl/style.md
