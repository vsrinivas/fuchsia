# benchmark

This directory contains Ledger benchmarks implemented as client apps connecting
to Ledger and using [tracing](https://fuchsia.googlesource.com/tracing/) to
capture performance data.

To run any of the benchmarks, run the trace util passing the benchmark binary
along with its parameters as the app to run. For example:

```
trace record --categories=benchmark,ledger \
  ledger_benchmark_put --entry-count=10 --value-size=100
```

Then retrieve the produced trace file using `netcp`.
