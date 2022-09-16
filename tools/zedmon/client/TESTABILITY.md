# Testability

The target `zedmon_client_manual_test` should be run manually as a part of any
CL that modifies this directory. It tests the surface of the CLI, which
requires an attached Zedmon device in order to test.

The underpinnings of the client support standard automated testing based on a
fake USB interface.

## TODO(fxbug.dev/109244)
The test is currently removed from the build graph until it can be re-implemented in a supported
(non-Dart) language.