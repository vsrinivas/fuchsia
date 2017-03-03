# benchmark

This directory contains Ledger benchmarks implemented as client apps connecting
to Ledger and using [tracing](https://fuchsia.googlesource.com/tracing/) to
capture performance data.

The benchmarks can be traced directly, as any other app would be, for example:

```
trace record --categories=benchmark,ledger \
  ledger_benchmark_put --entry-count=10 --value-size=100
```

, or using a JSON trace specification file:

```
trace record --spec-file=/system/data/ledger/benchmark/put.tspec
```
