# Where's this example?

If you're using Rust with DriverTestRealm we heavily recommend that you
use RealmBuilder to set up a new component per test instance
[(See example)](//examples/driver_test_realm/realm_builder/rust/).

This will make each test case hermetic and will be a better experience.

## Why isn't there a 'normal' DriverTestRealm example for Rust?

Each rust test is run in its own sub-process. There is no good way in
Rust to run a global function to setup the DriverTestRealm before the tests
are run.

## What if I really want to have a single DriverTestRealm that's shared between tests?

If that's the case then check the [Simple example](//examples/driver_test_realm/simple/rust).
Please note though that rust tests will be running simultaneously and extra care
needs to be taken to prevent data races with your drivers.

If you need to configure your DriverTestRealm, then create a new component
that starts it (see the implementation of [Simple](//sdk/lib/driver_test_realm/simple))
for more information.