# Composite Test

This test checks that DFv2 Device Groups work, and that services are
routed correctly from the nodes to the composite driver.

This test has a 'root' driver which creates two nodes, a left node and a right
node. It will also create a Device Group that matches their properties and the
`leaf` composite driver.

The 'root' driver exposes a service instance 'fuchsia.devicegroup.test.Service'
to each node. The left instance will always return the number 1 and the
right instance will always return the number 2.

The test checks that both service instances are routed and renamed correctly
when they go to the composite driver. The instances should be renamed based
on the node names in the 'leaf' driver's bind rules. The 'leaf' driver
should also be able to see a 'default' instance that represents its primary
parent's service instance.
