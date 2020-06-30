# Cobalt Test Application

This is an application that acts as an integration test for Cobalt. After
starting up, the application instantiates its own instance of the Cobalt
FIDL Service and connects to it. This application does not connect to the
instance of the Cobalt Service that is provided by the system environment.

The test application connects to the Cobalt FIDL service on two protocols:
Cobalt's standard `Logger` interface and also a special
test-only interface called
[Controller](/sdk/fidl/fuchsia.cobalt/cobalt_controller.fidl). The test
application uses the `Logger` interface to log metrics data to Cobalt and
uses the `Controller` interface to interogate Cobalt to determine if
it responds correctly to the `Logger` calls.