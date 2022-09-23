# V1 Lifecycle test

This test checks that DFv2 drivers work in a DFv1 environment.
DTR is set up without the DFv2 flag set.
There is a root driver that is a DFv1 driver.
The lifecycle driver is a DFv2 driver.

Lifecycle binds to root and connects to fuchsia.driver.compat.Service
to get its correct topological path. It exports itself to /dev/.
