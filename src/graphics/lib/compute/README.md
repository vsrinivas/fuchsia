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
fx set core.chromebook-x64                                                                 \
   --with //src/graphics/lib/compute:vulkan-tests                                          \
   --with //src/graphics/lib/compute:compute-benchmarks                                    \
   --args='core_realm_shards += [ "//src/graphics/lib/compute:compute-benchmarks-shard" ]' \
   --release                                                                               \
   --auto-dir
```

# Resolve the `/core/compute-benchmarks` instance

```
ffx component resolve /core/compute-benchmarks
```

# Explore the `/core/compute-benchmarks` instance

```
ffx component explore -l namespace /core/compute-benchmarks
$ ls pkg/bin
bench-vk
radix-sort-vk-bench
spinel-vk-bench
```

# Run `bench-vk`:

```
$ bench-vk
```

# Run `radix-sort-vk-bench`:

```
$ radix-sort-vk-bench 8086:591C pkg/data/targets/radix_sort_vk_intel_gen8_u32_resource.ar direct 16384 1048576
```

# Run `spinel-vk-bench`

If you want to execute `spinel-vk-bench` then SVG examples must be copied to the
device before exploring the component.

This script will copy a file to the `/cache` directory of the
`core/compute-benchmarks` instance:

```
#
# Copy one or more wildcarded files to the instance's /cache directory
#
spinel_vk_bench_copy_svg() {
  ccid=146cb8d457bc5856dffa25ad0ee38c17585c436915128283f3bb6dbf0e9c7c63
  for path in "$@"
  do
    for file in "$path"
    do
      if [ ! -e "$file" ]; then continue; fi
      base=$(basename $file)
      ccid_base=$ccid::$base
      echo "copy" $file "-->" "<ccid>::$base"
      ffx component storage --capability cache copy $file $ccid_base
    done
  done
}
```

# Execute `spinel-vk-bench`:

## Host:

```
$ wget -P /tmp https://upload.wikimedia.org/wikipedia/en/f/ff/UAB_Blazers_logo.svg
$ spinel_vk_bench_copy_svg /tmp/UAB_Blazers_logo.svg
$ ffx component explore -l namespace /core/compute-benchmarks
```

## Fuchsia:

```
$ spinel-vk-bench -h
$ spinel-vk-bench -f /cache/UAB_Blazers_logo.svg -Q -t 20 -r -c 100,100:2
$ <Ctrl-C to exit>
```

# Exit the component

```
$ exit
```
