# Introduction

Thin wrapper around the [Criterion benchmark suite]. This library is intended to be used as both a
simple means of generating benchmark data for the Chromeperf dashboard, but also for local
benchmarking with Criterion.

When generating Fuchsia benchmarking results (`FuchsiaCriterion::default`), the main function where
this constructor is called will expect an output JSON path where it will store the results in the
[Fuchsiaperf file format].

## Example - Criterion Bench

See [examples/main.rs] for an example of how to use the FuchsiaCriterion library to benchmark
Fuchsia targets.

Note that this example runs the benchmarks in an emulator, which results in highly variable
performance data that should be treated only as a rough approximation at best. Integrating the
benchmarks with Chromeperf will allow for precise results against specific devices, and enable
performance comparisons via the perfcompare tool.

### Running the Example

1. Add the criterion_bench target to your `fx set` line:

    ```
    fx set <product>.<board> --with //src/developer/fuchsia-criterion:criterion_bench
    ```
2. Build Fuchsia:

    ```
    fx build
    ```

3. Start the Fuchsia emulator:

    ```
    ffx emu start --headless
    ```

4. In a separate terminal, serve Fuchsia packages:

    ```
    fx serve -v
    ```

5. Connect to the emulator shell, and run the benchmarks, storing the output in
 `/tmp/criterion_bench_results.fuchsiaperf.json`:

    ```
    fx shell criterion_bench /tmp/criterion_bench_results.fuchsiaperf.json
    ```

### Local Bench
By default, the FuchsiaCriterion library will only output a fuchsiaperf.json file. However, adding
`--args=local_bench='true'` to your fx set line (e.g.  Step 1, above) overrides that behavior so
that FuchsiaCriterion provides the command line interface from the upstream Criterion library. This
allows you to replace Step 5 above with `fx shell criterion_bench`, and directly stream the
benchmark output to console.

[Criterion benchmark suite]: https://github.com/bheisler/criterion.rs
[Fuchsiaperf file format]: /docs/development/performance/fuchsiaperf_format.md
[examples/main.rs]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/developer/fuchsia-criterion/examples/main.rs
