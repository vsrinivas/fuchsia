# Fuchsia Filesystem Benchmarks

## Benchmarks

There are 8 benchmarks that get run for every filesystem. The currently supported filesystems are
Fxfs, F2fs, Memfs, and Minfs.

The benchmarks are all of the combinations of read/write, sequential/random, and warm/cold. Every
read/write call uses an 8KiB buffer and each operation is performed 1024 times spread across an 8MiB
file. The benchmarks measure how long each read/write operation takes.
* **Read**: makes `pread` calls to the file.
* **Write**: makes `pwrite` call to the file.
* **Sequential**: the reads/writes are performed sequentially from the start of the file to the end
  of the file.
* **Random**: the reads/writes are performed randomly across the entire file. Every part of the file
  is accessed exactly once.
* **Warm**: the reads/writes are performed on a file that was recently written and likely still
  cached in memory.
* **Cold**: the reads/writes are performed on a file that was not cached when the benchmark started.
  If the filesystem supports read-ahead then some of the operations may still hit cached data.

## "Cold" Benchmarks
At the beginning of most benchmarks is a setup phase that creates files within the filesystem.
Simply closing all handles to those files doesn't guarantee that the filesystem will immediately
clear all caches related to those files. If the caches aren't cleared then the benchmark may only
ever hit cached (warm) data. To support benchmarking uncached (cold) operations, the Fuchsia
Filesystem Benchmarks support remounting the filesystem. Remounting the filesystem between the setup
and recording phases guarantees that all data related the file that isn't normally cached gets
dropped.

## Framework
The Fuchsia Filesystem Benchmarks use a custom framework for timing filesystem operations.
Filesystems hold state external to the `read` or `write` operations being benchmarked which can lead
to drastically different timings between consecutive operations. For other performance tests, we
want to treat the initial one or more iterations as warm-up iterations and drop their timings. (For
example, for some IPC performance tests, the initial iteration doesn't complete until a subprocess
has finished starting up, making it much slower than the later iterations.) These storage tests
differ in that we don't want to drop the initial iterations' timings.

> Ex. On the first `read` operation to a file in Minfs, Minfs reads the entire file into memory and
> each subsequent `read` is served from memory. The warm-up phase of [fuchsia-criterion] would hide
> the extremely slow `read` call.

## Running the Benchmarks
1. Include `//src/storage/benchmarks` in `fx set`.
2. Run `fx test fuchsia-pkg://fuchsia.com/storage-benchmarks#meta/storage-benchmarks.cm`

The set of benchmarks and filesystems can filtered with the `--filter` flag.

[fuchsia-criterion]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/developer/fuchsia-criterion
