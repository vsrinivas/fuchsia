# transit_bench

Some benchmarks to measure different streaming binary transports.

## Getting Started

### Building

Include `//src/diagnostics/transit_bench` in your current universe packages (`fx set`/`fx args`).
Make sure that you are building with optimizations (`fx set --release` or `is_debug = false` in
`fx args`).

Generate a Cargo.toml for your editor to use:

```
fx build //src/diagnostics/transit_bench:bin_cargo
fx gen-cargo //src/diagnostics/transit_bench:bin
```

### Documentation

`fx rustdoc src/diagnostics/transit_bench:bin --open`

### Running benchmarks

`fx test transit_bench`

### Viewing logs without spam

`fx syslog --suppress bogus`

### Formatting

Minimum before submitting a CL: `fx rustfmt //src/diagnostics/transit_bench:bin`.

Prefer `fx format-code` if possible.
