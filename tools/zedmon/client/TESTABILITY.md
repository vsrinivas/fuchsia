# Testability

The target `zedmon_client_manual_test` should be run manually as a part of any
CL that modifies this directory. It tests the surface of the CLI, which
requires an attached Zedmon device in order to test.

The underpinnings of the client support standard automated testing based on a
fake USB interface.
