# Netstack3 Benchmarks

Netstack3 currently has microbenchmarks exercising the internals of Core,
but in the future this may be expanded to include both the Bindings and the
public API surface. Netstack3 benchmarks are based on the
[Criterion](https://docs.rs/criterion/latest/criterion/) benchmark
infrastructure. In Core, benchmark functions are declared with the
[`bench!` macro](https://cs.opensource.google/search?q=f:netstack3%20%22macro_rules!%20bench%22&ss=fuchsia)
 and can be run either as standard unit tests (picked up both by
`cargo test` and `fx test`), or to profile the performance of Netstack3. As
unit tests, the benchmarks may perform debug assertions to verify the
correctness of Netstack3. When run as performance benchmarks, the build is
optimized and all debug assertions are stripped, allowing the benchmark to
measure Netstack3's performance in a production-like environment.

## Running the Benchmarks

1. Add the benchmark binary target to your `fx set` line, and configure your
   build for release (optimized):

    ```
    fx set <product>.<board> ... --with //src/connectivity/network/netstack3:netstack3_benchmarks --release
    ```

2. Build Fuchsia

    ```
    fx build
    ```

3. Start the Fuchsia emulator

    ```
    ffx emu start --headless
    ```

4. In a separate terminal, serve Fuchsia packages

    ```
    fx serve -v
    ```

5. Connect to the emulator shell

    ```
    fx shell
    ```

6. Run the benchmarks, storing the output in `/tmp/ns3_benchmark_results.json`:

    ```
    $ netstack3_benchmarks /tmp/ns3_benchmark_results.json
    ```