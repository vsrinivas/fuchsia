# benchmark

## Overview
This directory contains Ledger [trace-based benchmarks]. Each benchmark is
implemented as a client connected to Ledger and using its API to perform one or
more of its basic operations (creating a page, writing/deleting entries, ...).

## Run benchmarks

### Using trace spec files
The easiest way to run a benchmark is using the associated trace spec file,
for example:

```
trace record --spec-file=/system/data/ledger/benchmark/put.tspec
```

You can read more on the trace spec file format in the [trace-based benchmarking
guide](https://fuchsia.googlesource.com/garnet/+/master/docs/benchmarking.md#Specification-file).

### Tracing benchmark apps directly
You can also trace the app directly:

```
trace record --categories=benchmark,ledger ledger_benchmark_put \
  --entry-count=10 --transaction-size=1 --key-size=100 --value-size=100 \
  --refs=off
```

That would generate a trace result file (default: `/data/trace.json`), which can
be analysed manually or using special tools.

### Benchmarks using sync
Some benchmarks exercise sync. To run these, pass the ID of a correctly
[configured] Firebase instance to the benchmark binary. For example:

```
trace record --spec-file=/system/data/ledger/benchmark/sync.tspec \
  --append-args=--server-id=<my instance>
```

### A note regarding benchmark apps
Since the benchmark apps are designed to be used with tracing, running them
without a tracing will not generate any results.

## List of benchmarks

Some of the benchmark apps have several corresponding tspec files, to exercise
different possible scenarios of using Ledger. For example, `get_page` benchmark
is used in `add_new_page.tspec` to emulate creating many pages with different
IDs, and in `get_same_page.tspec` to create several connections to the same
page.

### Local benchmarks
* __Get page__: How long does it take to establish a new page connection?
    * `add_new_page.tspec`: connection to the new page with previously unused
      ID.
    * `get_same_page.tspec`: several connection to the same page
* __Put__: How long does it take to write data to a page? And how long before the
  client will receive a [PageWatcher notification] about its own change?
    * `put.tspec`: basic case
    * `put_as_reference.tspec`: entries are put as references (CreateReference +
      PutReference)
    * `transaction.tspec`: changes are made in a transaction
* __Update entry__: How long does it take to update an existing value in Ledger
  (make several Put operations with the same key, but different values)?
    * `update_entry.tspec`: basic case
    * `update_entry_transactions.tspec`: changes are grouped in transactions
* __Delete entry__: How long does it take to delete an entry from a page?
    * `delete_entry.tspec`: each entry is deleted separately (outside of a
      transaction)
    * `delete_entry_transaction.tspec`: deletions are grouped in transactions

### Sync benchmarks
These benchmarks exercise synchronisation and need an ID of a Firebase instance
passed to them as described [in a previous
section](README.md#benchmarks-using-sync).

* __Backlog__: How long does it take to download all existing data when
  establishing a new connection to an already populated data?
    * `backlog.tspec`: basic case
    * `backlog_big_entry.tspec`: page contains one entry, but of a big size
    * `backlog_big_entry_updates.tspec`: one big entry, but that was updated several
      times prior to the new connection (commit history)
    * `backlog_many_big_entries.tspec`: page contains several big entries
    * `backlog_many_small_entries.tspec`: many small entries
    * `backlog_small_entry_updates.tspec`: small entry, but a long commit
      history
* __Convergence__: Several devices make concurrent changes to the page. How long does
  it take for all devices to see each other changes?
    * `convergence.tspec`: two devices
    * `multidevice_convergence`: several devices
* __Fetch__: How long does it take to fetch a [lazy value]?
    * `fetch.tspec`: basic case
    * `fetch_partial.tspec`: using FetchPartial (fetch in several parts)
* __Sync__: When one device makes changes to the page, how long does it take for
  another one to receive these changes?
    * `sync.tspec`: basic case
    * `sync_big_change.tspec`: syncing a big change (containing several write
      operations)

[trace-based benchmarks]: https://fuchsia.googlesource.com/garnet/+/master/docs/benchmarking.md
[configured]: /docs/ledger/firebase.md
[lazy value]: /docs/ledger/api_guide.md#lazy-values
[PageWatcher notification]: /docs/ledger/api_guide.md#watch
