# Calculator example

Reviewed on 2022-10-7

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

To add this project to your build, append the following to your `fx set`
invocation:

```
fx set workstation_eng.qemu-x64\
    --with //examples/fidl/calculator:all\
    --with //examples/fidl/calculator:tests
```

and then build:

```
fx build
```

## Running

Start the emulator and package server.

1. start the emulator:

```
ffx emu start --headless workstation_eng.qemu-x64
```

1. start the package server:

```
fx serve-updates
```

After the emulator & package server are running, the instructions differ based
on language.

### Rust

Between, [client][client-rs], [server][server-rs], [fidl][fidl-component], and
[realm][realm], all necessary services are provided to run a simple
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

### C++

Between, [client][client-cpp], [server][server-cpp], [fidl][fidl-component], and
[realm][realm], all necessary services are provided to run a simple
calculator on Fuchsia.

To run these components together add `calculator-example-cpp` to the ffx-laboratory:

```
ffx component create /core/ffx-laboratory:calculator-example-cpp \
    fuchsia-pkg://fuchsia.com/calculator-example-cpp#meta/calculator_realm.cm
```

and then start the component:

```
ffx component start /core/ffx-laboratory:calculator-example-cpp
ffx component start /core/ffx-laboratory:calculator-example-cpp/client
```

To see the output, use `ffx log`:

```
ffx log --filter calculator
```

You should see output like the following:

-  **Rust**
```
[186076.236][pkg-resolver][pkg-resolver][I] resolved fuchsia-pkg://fuchsia.com/calculator-example-rust as fuchsia-pkg://default/calculator-example-rust to 0e02791fffe45549315e420ee5bbcd4de6b8002f9e791b55cb6a09439faee266 with TUF
[186076.374][client][][I] 1 + 1 = 2
[186076.376][client][][I] -1 + -1 = -2
[186076.377][client][][I] 3.333 + 4.444 = 7.777
[186076.379][client][][I] 4 - 4 = 0
[186076.380][client][][I] 5.67 * 8.39 = 47.5713
[186076.381][client][][I] 15.07 / 7.23 = 2.0843706777316733
[186076.383][client][][I] 2 ^ 16 = 65536
```

-  **C++**

```
[185909.783][pkg-resolver][pkg-resolver][I] resolved fuchsia-pkg://fuchsia.com/calculator-example-cpp as fuchsia-pkg://default/calculator-example-cpp to 1d272f44d34d8b44084d5a3ccd78ec6c4688ba62579d87ed4e0437d5a46d143b with TUF
[185909.825][client][calculator_client][I]: [main.cc:54] Calculator client: operator()():  got response 2
[185909.825][client][calculator_client][I]: [main.cc:54] Calculator client: operator()():  got response -2
[185909.825][client][calculator_client][I]: [main.cc:54] Calculator client: operator()():  got response 7.777
[185909.825][client][calculator_client][I]: [main.cc:70] Calculator client: operator()():  got response 0
[185909.825][client][calculator_client][I]: [main.cc:86] Calculator client: operator()():  got response 47.5713
[185909.825][client][calculator_client][I]: [main.cc:102] Calculator client: operator()():  got response 2.08437
[185909.825][client][calculator_client][I]: [main.cc:117] Calculator client: operator()():  got response 65536
[185909.825][client][calculator_client][I]: [main.cc:154] Received all responses, shutting down client
[185909.824][server][calculator_server][I]: [main.cc:147] C++ calculator server has started!
[185909.825][server][calculator_server][I]: [main.cc:46] Calculator server: Add() a=1 b=1
[185909.825][server][calculator_server][I]: [main.cc:46] Calculator server: Add() a=-1 b=-1
[185909.825][server][calculator_server][I]: [main.cc:46] Calculator server: Add() a=3.333 b=4.444
[185909.825][server][calculator_server][I]: [main.cc:60] Calculator server: Subtract() a=4 b=4
[185909.825][server][calculator_server][I]: [main.cc:74] Calculator server: Multiply() a=5.67 b=8.39
[185909.825][server][calculator_server][I]: [main.cc:85] Calculator server: Divide()  dividend = 15.07 divisor=7.23
[185909.825][server][calculator_server][I]: [main.cc:96] Calculator server: Pow() base=2 exponent=16
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

**Client**

To run the client tests, ensure you have an available Fuchsia target (ex: `ffx
emu start`) and are serving packages (ex: `fx serve-updates`) then run the
following:

```
fx test calculator-client-cpp-parser-unittests
```

TODO(fxbug.dev/108591): Create client side FIDL unit tests once the TestBase exists for Natural bindings.

**Server**

To run the integration tests, which uses the C++ server as the component under test, run the following:

```
fx test calculator-integration-test-cpp
```

## Source Layout

The source is split into several parts:

1. [fidl][fidl-component] - defines the calculator FIDL protocol.
1. [realm] - wires up the FIDL, client, and server components together.
1. client - connects to a calculator FIDL protocol implementation and sends requests.
+ [rust client][client-rs]
+ [C++ client][client-cpp]
1. server - implements the calculator FIDL protocol and receives requests.
+ [rust server][server-rs]
+ [C++ server][server-cpp]
1. integration tests - Sets up a static realm with the server as the component under test
+ [C++ integration tests][integration-cpp]

[FIDL]: https://fuchsia.dev/fuchsia-src/development/languages/fidl
[fidl-component]: ./fidl/README.md
[realm]: ./realm/README.md
[client-rs]: ./rust/client/README.md
[server-rs]: ./rust/server/README.md
[client-cpp]: ./cpp/client/
[server-cpp]: ./cpp/server/
[integration-cpp]: ./cpp/integration_test/