# Fuchsia Vulkan Benchmarks

The Vulkan benchmarks include:

## Spinel SVG viewer: `spinel-vk-bench`

A benchmark that exercises the Vulkan API, the Vulkan device's display, and user
input.

It is GPU compute-bound or memory-bound with the CPU nearly idle.

For now, only basic SVGs are supported by the parser.

## Vulkan radix sorting library: `radix-sort-vk-bench`

A sorting benchmark that submits a number of intensive compute shaders onto the
Vulkan device and captures elapsed times.

For the same hardware any observed differences across hosts should be minimal
unless the driver’s codegen is different.

## CPU↔GPU latency measurement: `bench-vk`

A microbenchmark that captures the round trip time of a GPU submission and
completion using several different submit-and-wait Vulkan mechanisms.

This benchmark quantifies the overhead and latency of Magma/FIDL.

# Build

```
(host)$ fx set core.chromebook-x64                                                         \
   --with //src/graphics/lib/compute:vulkan-tests                                          \
   --with //src/graphics/lib/compute:compute-benchmarks                                    \
   --args='core_realm_shards += [ "//src/graphics/lib/compute:compute-benchmarks-shard" ]' \
   --release                                                                               \
   --auto-dir
```

# Resolve the `/core/compute-benchmarks` instance

```
(host)$ ffx component resolve /core/compute-benchmarks
```

# Explore the `/core/compute-benchmarks` instance

```
(host)$ ffx component explore -l namespace /core/compute-benchmarks
$ ls pkg/bin
bench-vk
radix-sort-vk-bench
spinel-vk-bench
```

# Run `bench-vk`:

```
(host)$ ffx component explore -l namespace /core/compute-benchmarks
$ bench-vk
```

# Run `radix-sort-vk-bench`:

```
(host)$ ffx component explore -l namespace /core/compute-benchmarks
$ radix-sort-vk-bench 8086:591C pkg/data/targets/radix_sort_vk_intel_gen8_u32_resource.ar direct 16384 1048576
```

# Run `spinel-vk-bench`:

There are three steps:

1. Get an example SVG file.
1. Copy it to the `/core/compute-benchmarks` instance.
1. Run `spinel-vk-bench` for 20 seconds while rotating around (100,100) and
   scaled by 2.0.  The Vulkan Validation layers are disabled with `-Q`.

```
(host)$ wget -P /tmp https://upload.wikimedia.org/wikipedia/en/f/ff/UAB_Blazers_logo.svg
(host)$ ffx component copy /tmp/UAB_Blazers_logo.svg /core/compute-benchmarks::/cache
(host)$ ffx component explore -l namespace /core/compute-benchmarks
$ spinel-vk-bench -f /cache/UAB_Blazers_logo.svg -Q -t 20 -r -c 100,100:2
$ <Esc on device or Ctrl-C on host to exit>
```
