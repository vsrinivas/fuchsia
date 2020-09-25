# ffx self-test

The ffx self-test plugin contains and executes a suite of end to end
tests that ffx can run against itself.

The test suite may have specific requirements or dependencies on the
environment in which it is run. The details of environment
reconfiguration and test selection are not yet decided.

The test process requires at least one target device available, and
the target device should be capable of running RCS, and reachable
over ssh, discoverable by mdns. The core product configuration is a
good choice today.

