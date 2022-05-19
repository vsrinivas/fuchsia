# Driver-Index

The Driver Index manages the list of drivers that exist in a Fuchsia system. The Driver Index
vends the
[fuchsia.driver.index.DriverIndex](/sdk/fidl/fuchsia.driver.index/driver_index.fidl) FIDL.

Driver Manager uses the Driver Index to find a Driver to bind to a given Device. Driver Index does
this by running all of the Driver
[bind programs](/docs/concepts/drivers/device_driver_model/driver-binding.md) against a given
Device and returning the Driver that matches the Device the best.

Driver Index will be used by the Drivers-As-Components effort.

## Building and Running

When the Driver Index is complete, it will be built in every product and launched during startup.

## Testing

Unit tests for the Driver Index are available in the `driver-index-unittests` package.

```
$ fx test driver-index-unittests
```

