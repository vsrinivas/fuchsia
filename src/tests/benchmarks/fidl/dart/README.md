# Dart FIDL Microbenchmarks

This is a small set of benchmarks that we use to evaluate changes to the Dart
FIDL bindings, in particular encoding and decoding.

So far it contains benchmarks for:
 - string encoding and decoding, both ASCII and Unicode

You can include this in your build by including the target:
`//src/tests/benchmarks/fidl/dart`.  If you use `fx` that means
passing `--with //src/tests/benchmarks/fidl/dart` to `fx set`.

You can build the benchmarks by invoking:

    fx build src/tests/benchmarks/fidl/dart

This is mainly useful for checking for build errors. If you would like to build
and then run the benchmark, use:

    fx test -v dart-fidl-benchmark

This is most useful while considering whether to land a change to the bindings.

If you are integrating this tool with something that expects output in the
[benchmark results schema][schema], you can pass an argument to the
`--output_file` flag. You would need a way to access the test file after
writing it out, for example you can run the benchmark in a specific realm
using `run-test-component` in order to persist the file:

    fx shell run-test-component --realm-label=fidl_benchmarks fuchsia-pkg://fuchsia.com/dart-fidl-benchmarks#meta/dart-fidl-benchmarks.cmx --out_file /data/results.json

You could then scp the file off of the device:

    fx scp "[$(fx get-device-addr)]:/data/r/sys/r/fidl_benchmarks/fuchsia.com:dart-fidl-benchmarks:0#meta:dart-fidl-benchmarks.cmx/results.json" results.json

<!-- xref -->
[schema]: /docs/development/benchmarking/results_schema.md
