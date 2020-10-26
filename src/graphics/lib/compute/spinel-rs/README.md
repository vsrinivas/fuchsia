# Introduction

Safety wrapper over [Spinel] in Rust.

Spinel is a vector renderer that takes advantage GPU parallelism in order to
maximize efficiency. It does this with the help of a scene-graph-like
asynchronous API and by dividing the scene into tiles. Each tile is then
processed by means of simple styling commands that run on VMs in parallel.

It currently runs on Vulkan 1.1 on a number of GPU architectures.

## Goals

- [ ] complete Spinel access

  Should cover the complete Spinel API with minimal overhead by making use of
  types and zero-cost abstractions wherever possible. Managed abstractions are
  also provided for safety and commodity.

- [ ] memory-safety

  Whether or not used correctly, this API should not cause undefined behavior,
  leak memory, and should always fail gracefully.

- [ ] provide test bed for Spinel's API

  Should test Spinel's public API externally in order to improve API
  understanding, find edge cases, and avoid regression.

- [ ] complete documentation

  Apart from explaining functionality, the crate should provide comprehensive
  error documentation and examples.

## Documentation

Temporary way of generating documentation:

```shell
fx gen-cargo . && cargo doc --open
```

[Spinel]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/
