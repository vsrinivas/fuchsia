# Calculator example

Reviewed on 2022-05-12

This directory contains the necessary Fuchsia components to run a client/server
implementation of a calculator.

The main goals of this example are to:

+ Demonstrate communicating between Fuchsia components using [FIDL].
+ Demonstrate FIDL best practices.
+ Be realistic, even if it means more complex code.

## Building

The build steps differ slightly depending on if you're using the rust or c++
version of the code. Both are functionally the same, so use the language you're
most familiar with or interested in.

### Rust

To add this project to your build, append the following to your `fx set`
invocation:

```
fx set workstation.qemu-x64\
    --with //examples/fidl/calculator:all\
    --with //examples/fidl/calculator:tests
```

and then build:

```
fx build
```

### C++

TODO(fxbug.dev/103280)

## Running

Start the emulator and package server.

1. start the emulator:

```
ffx emu start --headless workstation.qemu-x64
```

1. start the package server:

```
fx serve-updates
```

After the emulator & package server are running, the instructions differ based
on language.

### Rust

Between, [client][client-rs], [server][server-rs], [fidl][fidl-component], and
[realm][realm-rs], all necessary services are provided to run a simple
calculator on Fuchsia.

To run these components together add `calculator-example-rust` to the ffx-laboratory:

```
ffx component create /core/ffx-laboratory:calculator-example-rust \
    fuchsia-pkg://fuchsia.com/calculator-example-rust#meta/calculator_realm.cm
```

and then start the component:

```
ffx component start /core/ffx-laboratory:calculator-example-rust
ffx component start /core/ffx-laboratory:calculator-example-rust/client
```


To see the output, use `ffx log`:

```
ffx log --filter calculator
```

## Testing

There are tests for both the client and server. The command for running tests
differs slightly depending on the language used.

### Rust

**Client**

To run the client tests, ensure you have an available Fuchsia target (ex: `ffx
emu start`) and are serving packages (ex: `fx serve-updates`) then run the
following:

```
fx test calculator-client-rust-unittests
```

**Server**

To run the server tests, ensure you have an available Fuchsia target (ex: `ffx
emu start`) and are serving packages (ex: `fx serve-updates`) then run the
following:

```
fx test calculator-server-rust-unittests
```

### C++

TODO

## Source Layout

The source is split into four parts:

1. [fidl][fidl-component] - defines the calculator FIDL protocol.
1. [realm] - wires up the FIDL, client, and server components together.
1. client - connects to a calculator FIDL protocol implementation and sends requests.
+ [rust client][client-rs]
1. server - implements the calculator FIDL protocol and receives requests.
+ [rust server][server-rs]

[FIDL]: https://fuchsia.dev/fuchsia-src/development/languages/fidl
[fidl-component]: ./fidl/README.md
[realm-rs]: ./rust/realm/README.md
[client-rs]: ./rust/client/README.md
[server-rs]: ./rust/server/README.md
