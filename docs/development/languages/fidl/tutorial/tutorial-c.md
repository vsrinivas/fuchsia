
# C language FIDL tutorial

## About this tutorial

This tutorial describes how to make client calls and write servers in C
using the FIDL InterProcess Communication (**IPC**) system in Fuchsia.

Refer to the [main FIDL page](../README.md) for details on the
design and implementation of FIDL, as well as the
[instructions for getting and building Fuchsia](/docs/getting_started.md).

# Getting started

We'll use the `echo.fidl` sample that we discussed in the [FIDL Tutorial](README.md)
introduction section, by opening
[//garnet/examples/fidl/services/echo.fidl](/garnet/examples/fidl/services/echo.fidl).

<!-- NOTE: the code snippets here need to be kept up to date manually by
     copy-pasting from the actual source code. Please update a snippet
     if you notice it's out of date. -->


```fidl
library fidl.examples.echo;

[Discoverable]
protocol Echo {
    EchoString(string? value) -> (string? response);
};
```

## Build

Use the following steps to build:

(@@@ to be completed)

## `Echo` server

(@@@ to be completed)

## `Echo` client

(@@@ to be completed)

