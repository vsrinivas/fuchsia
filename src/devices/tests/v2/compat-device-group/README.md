# Compat Device Group Test

This integration test verifies that the compat shim supports device
groups. 

It first creates a 'root' driver, which adds a device group and two
child nodes that bind to its nodes.

After gathering its node, the composite formed from the device group
binds to the 'device-group' driver. When the driver starts, it sends
an ack through the fuchsia.compat.devicegroup.test.Waiter protocol.

The test waits until it receives an ack response from the
'device-group' driver.
