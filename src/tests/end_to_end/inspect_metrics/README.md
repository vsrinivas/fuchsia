# Inspect Metrics Tests

## What is this test?

The inspect\_metrics end to end tests ensure that expected data is
present in the Inspect output of particular components.

Inspect is used to report per-component health and status. The values
tested in this E2E test are needed by downstream components for reporting.

This test ensures that the expected data is available on real systems,
which is not covered by integration tests.

## Fuchsia Components involved

Each of the listed components below expose Inspect data that is read
through the Archivist for the test.

### Archivist

The [Archivist](/src/diagnostics/archivist) is the core of the Component
Diagnostics platform. It is responsible for handling requests for
diagnostics data from components and returning it in a structured payload.

These E2E tests all read Inspect data directly from the Archivist and
ensure that expected values are set for individual components.

The Archivist exposes diagnostics data about itself, which is asserted in
`archivist_metrics_test.dart`.

The Archivist provides interfaces for All data and data filtered for
indiviual use cases. The `archivist_reader_test.dart` checks that these
pipelines are usable.

#### Other Tests

Integration and unit tests for this functionality are present at 
[//src/diagnostics/archivist/tests][archivist-tests]

### fshost

`archivist_reader_test.dart` and `inspect_metrics_test.dart` assert
that filesystem usage stats are exposed by the fshost component through
the Archivist.

`blobfs_metrics_test.dart` asserts that blobfs stats are exposed by the
fshost component through the Archivist.

#### Other Tests

Unit tests for this functionality are present at 
[//src/storage/fshost/inspect-manager-test.cc][fshost-tests]

### memory\_monitor

`archivist_reader_test.dart` asserts that memory usage is exposed by
the memory\_monitor component through the Archivist.


#### Other Tests

Unit tests for this functionality are present at 
[//src/developer/memory/monitor/tests/][memory-tests]

### appmgr

appmgr is responsible for running the V1 components hierarchy. On behalf
of those components, it exposes the last hour of CPU usage sampled
every minute.

`appmgr_cpu_metrics_test.dart` asserts the data is exposed by
appmgr through the Archivist.


#### Other Tests

Unit tests for this functionality are present at 
[//src/sys/appmgr/cpu_watcher_unittest.cc][appmgr-tests]

[archivist-tests]: https://fuchsia.googlesource.com/a/fuchsia/+/master/src/diagnostics/archivist/tests/
[fshost-tests]: https://fuchsia.googlesource.com/a/fuchsia/+/master/src/storage/fshost/inspect-manager-test.cc
[memory-tests]: https://fuchsia.googlesource.com/a/fuchsia/+/master/src/developer/memory/monitor/tests/
[appmgr-tests]: https://fuchsia.googlesource.com/a/fuchsia/+/master/src/sys/appmgr/cpu_watcher_unittest.cc
