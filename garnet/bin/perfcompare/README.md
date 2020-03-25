# perfcompare: Performance comparison tool

## Dependencies

There are two ways to run the perfcompare tool with the required
dependencies (currently just the `scipy` Python library):

* Via `vpython`:

  ```sh
  ./prebuilt/third_party/vpython/vpython garnet/bin/perfcompare/perfcompare.py
  ```

  This will automatically download prebuilt, hermetic versions of
  dependencies.  `vpython` is used for running perfcompare on the
  Infra builders.

* On Linux, when using Debian/Ubuntu, the dependencies can be
  installed using APT:

  ```sh
  sudo apt-get install python-scipy
  ```

## Example: Running perf tests locally and comparing results

The perfcompare tool can be used to run perf tests locally and to
compare the results.

As an example, suppose you want to compare the results from
`tspec_example_test` (a simple example test) on two Git commits,
`BEFORE_VERSION` and `AFTER_VERSION`.

The following commands would gather a dataset of perf test results for
`BEFORE_VERSION` and save them in the directory `perf_results_before`:

```sh
git checkout BEFORE_VERSION
fx build
fx update
python garnet/bin/perfcompare/perfcompare.py run_local \
  --boots=5 \
  --iter_cmd='fx run-e2e-tests tspec_example_test' \
  --iter_file='out/test_out/*/*.fuchsiaperf.json' \
  --dest=perf_results_before
```

These commands would do the same, but for `AFTER_VERSION`, saving the
results dataset in a different directory, `perf_results_after`:

```sh
git checkout AFTER_VERSION
fx build
fx update
python garnet/bin/perfcompare/perfcompare.py run_local \
  --boots=5 \
  --iter_cmd='fx run-e2e-tests tspec_example_test' \
  --iter_file='out/test_out/*/*.fuchsiaperf.json' \
  --dest=perf_results_after
```

Note that the `run_local` commands will reboot Fuchsia.

The two datasets can then be compared with the following command,
which prints a table showing the "before" and "after" results side by
side:

```sh
python garnet/bin/perfcompare/perfcompare.py compare_perf perf_results_before perf_results_after
```
