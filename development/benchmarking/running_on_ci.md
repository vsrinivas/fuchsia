# Running Benchmarks on Continuous Integration (CI)

* Owner: kjharland@
* Created: 2018 Apr 20
* Updated: 2018 Apr 20

[TOC]


## Overview

This guide will walk you through the process of adding a benchmark to CI so that its results are tracked over time in the performance dashboard.
Today this process is supported for:
* Garnet
* Peridot
* Topaz

Note that Zircon benchmarks run in the Garnet layer.

The reader should be familiar with the following:

1. [Building and running Fuchsia](https://fuchsia.googlesource.com/docs/+/master/getting_started.md)
1. [runbenchmarks](https://fuchsia.googlesource.com/garnet/+/master/testing/runbenchmarks/README.md)
1. [The Catapult Dashboard](#) TODO(kjharland): Add public docs.

### Terminology

* __$layer__: The layer-cake project that your benchmark exercises.  One of Zircon, Garnet, Peridot, or Topaz.

## Requirements

You can automate your benchmark as long as its results use the same [schema](https://fuchsia.googlesource.com/zircon/+/master/system/ulib/perftest/performance-results-schema.json) as Tracing based benchmarks.

## Tutorial: Add Your Benchmark to CI

### 1. Locate your benchmark files on the Fuchsia image

We don't yet have a system that computes the locations of benchmark files from their respective GN targets, and adds those to the pipeline.  For now, you must figure out the full path to your benchmark files on the Fuchsia image.  When you're updating the benchmark script in the next step, you will use this path to your tspec file, benchmark binary, etc.

### 2. Update the $layer benchmark script

Each `$layer` project has a shell script that runs all benchmarks for that project.  This is the file you'll modify to add your benchmark.  These scripts are located at:

* Garnet: [//garnet/bin/benchmarks/benchmarks.sh](https://fuchsia.googlesource.com/garnet/+/master/bin/benchmarks/benchmarks.sh)
* Peridot: [//peridot/tests/benchmark/peridot_benchmarks.sh](https://fuchsia.googlesource.com/peridot/+/master/tests/benchmark/peridot_benchmarks.sh)

Add a command to this file to execute your test, see those scripts for examples.

__NOTE:__ The name you select as the output file will be the name of the test-suite for your benchmark results in the Catapult dashboard.  Choose this name wisely and do not change it often, since it is used for long-term regression detection.

### 3. Test the build

At this point, you're ready to build Fuchsia and test that your benchmark runs successfully. Run the following in a shell:

```sh
fx set-layer $layer
jiri update -gc
# Benchmarks are not included in production packages, so use $layer/packages/kitchen_sink or they will not be built.
fx set <arch> --packages $layer/packages/kitchen_sink
fx full-build && fx run
```

Once the Fuchsia shell is loaded, you can run the benchmarks by hand. The scripts that run all of the benchmarks for `$layer` are in the fuchsia image at:

```sh
/pkgfs/packages/${layer}_benchmarks/0/bin/benchmarks.sh
```

Run this script and specify /tmp as the output directory. For example:

```sh
/pkgfs/packages/garnet_benchmarks/0/bin/benchmarks.sh /tmp
```

If no errors occurred, you should see a file at `/tmp/$benchmark`, where `$benchmark` is the filename you used for your benchmark results.

### 4. Commit changes

Infra will automatically pick up your changes.  When new commits land in the `$layer` project, the benchmark will run and its results will be uploaded to the performance dashboard.

## FAQ

### How should I name my benchmark?

Your benchmark name should consist of multiple segments of letters and underscores joined together by periods. For example:

part_a.part_b.part_c....

__Examples:__

* topaz.build.dashboard
* ledger.get_same_page
* zircon.benchmarks

The benchmark ID becomes the unique ID for its results in the Catapult dashboard but with "fuchsia." prepended. So an ID of "zircon_benchmarks" will show up in the dashboard as "fuchsia.zircon_benchmarks".
