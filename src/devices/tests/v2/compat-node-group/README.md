# Compat Device Group Test

This integration test verifies that the compat shim supports device
groups. 

It first creates a 'root' driver, which adds a node group and two
child nodes that bind to its nodes.

After gathering its node, the composite formed from the node group
binds to the 'node-group' driver. When the driver starts, it sends
an ack through the fuchsia.compat.nodegroup.test.Waiter protocol.

The test waits until it receives an ack response from the
'node-group' driver.
