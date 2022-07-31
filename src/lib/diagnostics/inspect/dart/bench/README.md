# Dart inspect benchmarks

This directory contains the code to the inspect benchmarks, written
in Dart.

This is run by the test in
`//src/tests/end_to_end/perf/test/dart_inspect_benchmarks_test.dart`.

# Running

```bash
$ fx set terminal.x64 --release \
  --with //src/tests/end_to_end/perf:test \
  --with //src/tests/end_to_end/perf:package_deps \
  --with-base //src/dart \
  --args='core_realm_shards += [ "//src/dart:dart_runner_core_shard" ]'
$ fx build
$ fx test --e2e host_x64/dart_inspect_benchmarks_test
```
