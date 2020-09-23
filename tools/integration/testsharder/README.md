# testsharder

**testsharder** is a tool that takes a single list of tests and splits that
list into one or more new lists ("shards") so that each shard only contains
tests that run on a single device type.

Specifically, testsharder takes as input (via the `-build-dir` flag) a
fuchsia build out directory containing a tests.json produced by the fuchsia
build. tests.json contains a list of JSON objects conforming to the schema of
the `TestSpec` struct from `//tools/build/tests.go`.

testsharder's output is another JSON file, whose location is specified by the
`-output-file` flag. This file contains a list of JSON objects conforming to
the schema of the `Shard` struct from
`//tools/integration/testsharder/shard.go`. Each shard has a name that will
end in a number if there are multiple shards with the same device type, e.g.
"QEMU-(1)".

testsharder's primary consumer is the
[infra recipes](https://fuchsia.googlesource.com/infra/recipes), specifically
the
[`fuchsia/build` recipe](https://fuchsia.googlesource.com/infra/recipes/+/fbb7310f9df6c98428010fd0b36110fdfe4b9bfa/recipes/fuchsia/build.py#294).
That recipe uses testsharder's output to schedule a set of Swarming tasks,
each of which runs the tests from one shard.

## Sharding algorithm

testsharder has two flags to control the size of shards:

*   `-max-shard-size` specifies a target number of tests for each shard. If set,
    testsharder will divide the input tests.json into shards of approximately
    this size.
    <!-- TODO(fxbug.dev/49286): Rename this to `-target-test-count` -->

*   `-target-duration-secs` specifies a target duration for each shard. If set,
    testsharder will divide the input tests.json into shards whose tests are
    expected to complete in approximately this amount of time. See the
    "Sharding by time" section below for details on how testsharder uses this
    flag.

It's invalid to specify both of these flags at the same time.

testsharder also has a `-max-shards-per-environment` flag that sets a hard
maximum on the number of shards for each device type. If it's impossible to
divide the input `tests.json` into fewer than the maximum number of shards
while satisfying `-target-duration-secs` or `-max-shard-size`, some or all of
the shards will exceed `-target-duration-secs` or `-max-shard-size`.

### Sharding by time

Along with `tests.json`, testsharder also reads a `test_durations.json` file
from the build output directory. This file is copied into the output
directory from the `//integration/infra/test_durations` directory, which
contains a file for every infra builder that runs tests using the
[`fuchsia/fuchsia` recipe](https://fuchsia.googlesource.com/infra/recipes/+/fbb7310f9df6c98428010fd0b36110fdfe4b9bfa/recipes/fuchsia/fuchsia.py).
These files are updated periodically with recent duration data for each test.

When the infrastructure does a build, it dynamically looks up the name of the
current builder and passes the corresponding test duration file into the
build via the `test_durations_file` GN argument. The file is then renamed to
`test_durations.json` and copied into the output directory by the
`test_durations` target from the root `//BUILD.gn`.

testsharder then reads `test_durations.json` from the build output directory
and uses the duration data to divide tests into shards of approximately equal
expected duration, using a greedy approximation algorithm for
[static multiprocessor scheduling](https://en.wikipedia.org/wiki/Multiprocessor_scheduling).

Within a shard, tests are ordered by expected duration descending (longest
tests first). However, this should be considered an infra implementation
detail and is subject to change.

If any test does not appear in `test_durations.json` (for example, because
the duration files have not been updated since it was added) then testsharder
will use the duration file entry with name `*` for that test. That entry's
data is an average of all existing tests' data. So any newly added tests will
be scheduled close to the middle of one of the shards.

### Determinism

Given an input `tests.json`, `test_durations.json`, and `-multipliers` file,
testsharder's output is completely deterministic.

However, adding, deleting, or renaming a test can completely change the
output. For example, adding a new test that takes an average amount of time
will add that test to the middle of one of the shards. This will generally
push some faster tests into new shards, leading to a complete reshuffling of
all faster tests between shards.

Updates to the duration files may also affect testsharder's output, so
duration file updates go through CQ before landing to ensure that they don't
cause any ordering-related test breakages.

<!-- TODO(olivernewman) document multipliers -->
