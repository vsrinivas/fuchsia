This is an integration test to confirm that the Harvester (on the Fuchsia
device) and the Dockyard (on the Host/desktop) can communicate. The test is
written as an end-to-end (e2e) test.

To run test:
$ fx set [...] --with //src/tests/end_to_end/harvester_dockyard:tests
$ fx build && fx run-e2e-tests harvester_dockyard_test


