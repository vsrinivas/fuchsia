# structured_config

This test has two main components:

1. a Fuchsia package, core realm shard, and a system assembly that add a
   component with known configuration values to the component topology
2. a host test which parses the audit report written by scrutiny and asserts
   that the known component's configuration was visible

The core shard is necessary for scrutiny to pull our test component into its
model of the component topology.

In order to avoid polluting the core product definition we build our own
`assembled_system()` based off of the same inputs as the one in
`//build/images/fuchsia`.
