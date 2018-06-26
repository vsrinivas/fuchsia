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
trace record --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/put.tspec
```

You can read more on the trace spec file format in the [trace-based benchmarking
guide](https://fuchsia.googlesource.com/garnet/+/master/docs/benchmarking.md#Specification-file).

### Tracing benchmark apps directly
You can also trace the app directly:

```
trace record --categories=benchmark,ledger \
  /pkgfs/packages/ledger_benchmarks/0/bin/ledger_benchmark_put \
  --entry-count=10 --transaction-size=1 --key-size=100 --value-size=100 \
  --refs=off
```

That would generate a trace result file (default: `/data/trace.json`), which can
be analysed manually or using special tools.

### Benchmark parameters

\*.tspec files specify the parameters with which benchmark apps are run. You can
override these by passing the "--append-args" argument to the `trace record`
tool.
Commonly used among all the ledger benchmarks are the following parameters:

* `entry-count` for the number of entries to perform operations on
* `unique-key-count` if the number of operations and the number of entries
  differ (i.e. some entries are overwritten), this denotes the number of unique
  entries in the page
* `key-size` for the size of the key (in bytes)
* `value-size` for the size of the value (in bytes)
* `refs` with the values `on` or `off` for the reference strategy: if set to
  `on`, entries will be put using `CreateReference`/`PutAsReference`, otherwise
  they will be treated as inline entries.
* `commit-count` for the number of commits made to the page
* `transaction-size` for the number of operations in a single transaction
* `server-id` for an ID of a cloud instance used for synchronisation (see
  below).

Unless the name of the benchmark suggest otherwise, default values are:
* `100` entries
* key size `100`
* value size `1000`
Benchmarks under `sync` and `convergence` use smaller number of entries and
smaller value size.

### Benchmarks using sync
Some benchmarks exercise sync. To run these, pass the ID of a correctly
[configured] Firebase instance to the benchmark binary. For example:

```
trace record --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/sync.tspec
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
    * `put_big_entry.tspec`: writing big entries to the page
    * `put_as_reference.tspec`: entries are put as references (CreateReference +
      PutReference)
    * `transaction.tspec`: changes are made in a transaction
* __Update entry__: How long does it take to update an existing value in Ledger
  (make several Put operations with the same key, but different values)?
    * `update_entry.tspec`: basic case
    * `update_big_entry.tspec`: put an entry of big size, then update its value
    * `update_entry_transactions.tspec`: changes are grouped in transactions
* __Delete entry__: How long does it take to delete an entry from a page?
    * `delete_entry.tspec`: each entry is deleted separately (outside of a
      transaction)
    * `delete_big_entry.tspec`: same as above, but for big entries
    * `delete_entry_transactions.tspec`: deletions are grouped in transactions
    * `disk_space_cleared_page.tspec`: how much space does ledger take after the
      page was cleared out (all the entries deleted in one transaction)?
* __Disk space__: How much disk space does ledger use to store pages, objects
  and commits?
    * `disk_space_empty_ledger.tspec`: empty ledger (with no pages)
    * `disk_space_empty_pages.tspec`: ledger containing only empty pages
    * `disk_space_entries.tspec`: ledger with one page containing some entries
    * `disk_space_updates.tspec`: ledger with one page containing only one
      entry, but long commit history
    * `disk_space_one_commit_per_entry.tspec`: ledger with one page containing
      several entries, each of them added in a separate commit

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
    * `disk_space_synced_entries.tspec`: how much disk space does ledger take on a
    writer and a reader device, when several entries have been written (in one
    commit) on one device and then downloaded on another?
    * `disk_space_synced_updates.tspec`: how much disk space does ledger take on a
    writer and a reader device, when several commits with updates has been made on
    one device and then downloaded on another?
* __Convergence__: Several devices make concurrent changes to the page. How long does
  it take for all devices to see each other changes?
    * `convergence.tspec`: two devices
    * `multidevice_convergence`: several devices
* __Fetch__: How long does it take to fetch a [lazy value]?
    * `fetch.tspec`: basic case
    * `fetch_partial_big_entry.tspec`: using FetchPartial (fetch in several
    parts) on a big entry
* __Sync__: When one device makes changes to the page, how long does it take for
  another one to receive these changes?
    * `sync.tspec`: basic case
    * `sync_big_change.tspec`: syncing a big change (containing several write
      operations)

[trace-based benchmarks]: https://fuchsia.googlesource.com/garnet/+/master/docs/benchmarking.md
[configured]: /docs/ledger/firebase.md
[lazy value]: /docs/ledger/api_guide.md#lazy-values
[PageWatcher notification]: /docs/ledger/api_guide.md#watch
