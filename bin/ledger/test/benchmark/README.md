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
trace record --categories=benchmark,ledger ledger_benchmark_put \
  --entry-count=10 --transaction-size=1 --key-size=100 --value-size=100
  --refs=auto
```

Some benchmarks exercise sync. To run these, pass the ID of a correctly
[configured] Firebase instance to the benchmark binary. For example:

```
trace record --spec-file=/system/data/ledger/benchmark/sync.tspec \
  --append-args=--server-id=<my instance>
```

The set of benchmarks under [put](put) run the Put benchmark multiple times,
to evaluate Ledger's performance over changes in different parameters:
- `entry_count`: evaluates the insertion performance over different values of
number of inserted entries.
- `entry_count_update`: evaluates the entry update performance over different
number of stored entries.
- `transaction_size`: measures the time to insert a fixed number of entries when
split in transactions with varying sizes.
- `key_size`: evaluates the insertion performance over different key sizes.
- `value_size`: evaluates the insertion performance over different value sizes.

Each of these benchmarks can be executed using the corresponding tspec file,
like for example:
```
trace record --spec-file=/system/data/ledger/benchmark/entry_count.tspec
```

or by tracing it directly. For example:

```
trace record --categories=benchmark,ledger launch_benchmark \
  --app=ledger_benchmark_put --test-arg=entry-count \
  --min-value=10 --max-value=100 --step=10
  --append-args="--transaction-size=1,--key-size=64,--value-size=1000,--refs=auto"
```

[configured]: https://fuchsia.googlesource.com/ledger/+/HEAD/docs/user_guide.md
