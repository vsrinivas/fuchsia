# Filesystem Host for Security Package Delivery Tests

This component provides a fake filesystem host implementation for security
package delivery tests. The implementation hosts, for example, a blobfs instance
based on an FVM volume produced by system assembly.

The component manifest is declared as an incomplete shard so that configuration
details can be determined by the underlying test.
