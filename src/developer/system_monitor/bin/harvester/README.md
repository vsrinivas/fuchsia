# System Monitor Harvester

The Harvester runs on the Fuchsia device, acquiring Samples (units of
introspection data) that it sends to the Host using the Transport system.

The Harvester should not unduly impact the Fuchsia device being monitored.
So the Harvester does not store samples. Instead the samples are moved to
the Dockyard as soon as reasonable.

## Running tests

Before running the tests, add `//bundles:tests` to your `fx set` (i.e. args.gn),
then execute:
```
$ fx run-test system_monitor_harvester_tests
```
If the test is not already built, `fx run-test` will built it before running it.
