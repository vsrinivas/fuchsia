# DevmgrIntegrationTestShim

This is a shim library to make it easier to transition DevmgrIntegrationTest tests
to using the DriverTestRealm. This library uses DriverTestRealm under the hood,
but exposes the same API that older tests are using.

New tests should use DriverTestRealm directly. Please see the
[examples](/examples/drivers/driver_test_realm).
