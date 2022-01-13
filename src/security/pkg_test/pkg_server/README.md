# Package Server for Security Package Delivery Tests

This component provides a fake on-device package server for security package
delivery tests that resolve packages over the network. The implementation is
configured with system base packages and an update package (along with blob
dependencies) to serve a particular system update output by system assembly.

The component manifest is declared as an incomplete shard so that configuration
details can be determined by the underlying test.
