# Fuchsia Shell

## Overview

The goal of the Fuchsia Shell project is to build a shell experience for
Fuchsia developers that is Fuchsia-first - that is, one that allows the user
to interact with a system running Fuchsia in a way that feels native to
Fuchsia, rather than being ported forward from some other system.  The shell
is designed to allow you to manipulate FIDL interfaces, namespaces, Zircon
objects, and other core Fuchsia concepts.

## Getting started

To get started with the shell, simply add `"//src/developer/shell:cliff"` to
the packages you build.  You can then invoke `cliff` from the `dash` command
line.  As the shell evolves, it will eventually replace `dash`, and you will
not have to take the extra step of adding it to your build.

## Shell Language

### Variable declaration

You can declare a variable with `var` (meaning it is mutable), or `const`
(meaning it is immutable).  Variables are declared with an initial value:

```
var a = 1
const b = 2
```

### Integers

Integers are arbitrary length two's complement integers, although our
implementation currently limits users to 64-bit signed two's complement.
Integer literals can be sequences of numbers (e.g., `12345`), or can be
separated into groups of 3 by the `_` character (e.g., `1_001` or
`123_456`).
