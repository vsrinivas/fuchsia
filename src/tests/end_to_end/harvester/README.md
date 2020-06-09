Test the overall runtime of the Harvester. For unit tests see the Harvester
source code directory.

To run test:
$ fx set [...] --with //src/tests/end_to_end/harvester:tests
$ fx build && fx run-e2e-tests harvester_test
