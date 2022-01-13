# Security Tests for Package Delivery

This directory implements a series of large integration tests that verify
security properties of the package delivery system. Most of these properties
encode the package delivery system's role in ensuring
[verified execution](/docs/concepts/security/verified_execution.md).

These are _target tests_, not _host tests_: tests run in a hermetic environment
on a target Fuchsia system, not on a development host machine.

## Test Structure

These tests instantiate the production implementation of package delivery
components in a production configuration (as apposed to development
configuration). All dependent components are faked to either:

1.  Simulate expected dependency behaviour, or
1.  Operate in an adversarial mode, simulating a dependency under attacker
    control.

See subdirectory `README.md` files for further details.
