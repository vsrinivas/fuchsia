# Security Package Delivery Test Case: Access OTA Blob as Executable

When a Fuchsia system downloads an OTA ("Over The Air" system update), the
contents of the update is not covered by the system's chain of verification
until the device reboots. This test simulates an OTA and attempts to use package
delivery APIs to map a blob from the OTA as executable. This procedure should be
prevented by the package delivery system to ensure
[verified execution](/docs/concepts/security/verified_execution.md).

## Test Composition

Detailed component composition is articulated in `.cml` files in the `meta/`
subdirectory. In general, the test brings up the `hello_world_v0` system
assembly in an isolated `fshost`, initiates an update to `hello_world_v1` via
an isolated `netstack` and `pkg_server`, then attempts to map the new version of
the "Hello, World!" executable as executable.
