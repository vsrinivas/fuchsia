# Driver Transport Example

This project contains examples that showcase the various FIDL transport options
available to drivers. Each project consists of a parent driver offering the
`fuchsia.examples.gizmo/Service` to a child driver over a given transport.

## Building

If these components are not present in your build, they can be added by appending
`--with //examples/drivers/transport` to your `fx set` command. For example:

```
$ fx set core.x64 --with //examples/drivers/transport
$ fx build
```

## Running

This project contains test packages to run each example parent/child driver
together in the driver test realm.

### Banjo transport

The `banjo` directory provides parent/child drivers that communicate using a
custom `fuchsia.examples.gizmo/Misc` protocol over the `Banjo` transport channel.

You can run the Banjo Transport sample inside the driver test realm using the
following command:

```
$ fx test banjo_transport_test
```

### Driver transport

The `driver` directory provides parent/child drivers that run co-located in the
same process and communicate using a custom `fuchsia.examples.gizmo/Device` FIDL
protocol over the in-process `Driver` transport channel:

-   `driver/v1`: Driver transport sample built using the DDK and DFv1.
-   `driver/v2`: Driver transport sample built using driver components and DFv2.

You can run the Driver transport samples inside the driver test realm using the
following command:

```
$ fx test driver_transport_test
```

### Zircon transport

The `zircon` directory provides parent/child drivers that run in separate host
processes and communicate using a custom `fuchsia.examples.gizmo/Device` FIDL
protocol over the `Zircon` transport channel:

-   `zircon/v1`: Zircon transport sample built using the DDK and DFv1.
-   `zircon/v2`: Zircon transport sample built using driver components and DFv2.

You can run the Zircon Transport samples inside the driver test realm using the
following command:

```
$ fx test zircon_transport_test
```
