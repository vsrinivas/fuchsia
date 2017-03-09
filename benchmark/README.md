# benchmark

This directory contains Ledger benchmarks implemented as client apps connecting
to Ledger and using [tracing](https://fuchsia.googlesource.com/tracing/) to
capture performance data.

Benchmarks can be run using the associated tracing spec files. For example:

```
trace record --spec-file=/system/data/ledger/benchmark/put.tspec
```

Benchmarks can also be traced directly, as any other app would be. For example:

```
trace record --categories=benchmark,ledger \
  ledger_benchmark_put --entry-count=10 --value-size=100
```

Some benchmarks exercise sync. To run these, make sure that Cloud sync is
[configured] correctly before running them. For example:

```
configure_ledger --firebase-id=<my instance id>
trace record --spec-file=/system/data/ledger/benchmark/sync.tspec
```

[configured]: https://fuchsia.googlesource.com/ledger/+/HEAD/docs/user_guide.md
