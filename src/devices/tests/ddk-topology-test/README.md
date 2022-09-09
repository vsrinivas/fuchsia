## ddk-topology-test

This test tests various driver topologies work on both DFv1 and DFv2.

For now it only tests the following scenario, where two nodes have the same name but are not siblings:

```
topology-grandparent
  /           \
parent1      parent2
  |            |
child        child
```
