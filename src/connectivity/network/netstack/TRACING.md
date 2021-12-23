# Netstack with tracing

You can select `netstack_with_tracing` component by switching the
network realm as follows:

```
base_package_labels -= [ "//src/connectivity/network" ]
base_package_labels += [ "//src/connectivity/network:network-with-tracing" ]
```
