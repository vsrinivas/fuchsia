# NOTE: For Rust, RealmBuilder is preferred over Simple

If you're using Rust with DriverTestRealm we heavily recommend that you
use RealmBuilder to set up a new component per test instance
[(See example)](//examples/driver_test_realm/realm_builder/rust/).

This will make each test case hermetic and will be a better experience.

Rust tests are each run in their own sub-process, meaning each test runs at
the same time. Using the Simple example, there is only one DriverTestRealm
component. The test author has to be very careful not to Set/Check data
on the same driver across different tests, otherwise there will likely
be data races.
