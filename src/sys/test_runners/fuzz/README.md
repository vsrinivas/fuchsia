# Fuzz Test Runner

The Fuzz Test Runner is a [test runner][test-runner] that creates a FIDL
connection between the fuzz-registry and the fuzzer component it launches. In
all other respects it is identical to the ELF Test Runner.

This test runner is useful for providing the fuzz-manager with a way to control
fuzzers running within the Test Runner Framework. The channel it installs in the
fuzzer breaks the hermeticity of the Test Runner Framework in a limited and
controlled manner. It allows the fuzzer to register a protocol with the
fuzz-registry that the fuzz-manager can use to connect a controller and drive
fuzzing workflows.

## Building

```bash
fx set core.x64 --with //src/sys/test_runners/fuzz
fx build
```

## Arguments

Fuzzing arguments are workflow-specific and may be changed at runtime via the
`Configure` method of the `fuchsia.fuzzer.Controller` protocol.

## Testing

Run:

```bash
fx set core.x64 --with //src/sys/test_runners/fuzz:tests
fx build
fx test fuzz-test-runner-tests
```

## Source layout

The entrypoint is located in `src/main.rs`, and the implementation of the
`ComponentLanucher` is in `src/launcher.rs`. All other code, including the FIDL
service implementation are a part of `//src/sys/test_runners/elf:lib`. Tests are
located within `tests/main.rs`.

[test-runner]: ../README.md
