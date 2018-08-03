Bluetooth
=========

The Fuchsia Bluetooth system aims to provide a dual-mode implementation of the
Bluetooth Host Subsystem (5.0+) supporting a framework for developing Low Energy
and Traditional profiles.

Source code shortcuts:
- [Public API](../../public/lib/bluetooth/fidl)
- [Private API](../../lib/bluetooth/fidl)
- [Tools](tools/)
- [Host Library](../../drivers/bluetooth/lib)
- [Host Bus Driver](../../drivers/bluetooth/host)
- [HCI Drivers](../../drivers/bluetooth/hci)
- [HCI Transport Drivers](https://fuchsia.googlesource.com/zircon/+/master/system/dev/bluetooth?autodive=0)

For more orientation, see
- [System Architecture](../../docs/bluetooth_architecture.md)
- [Detailed Source Layout](../../docs/bluetooth_source_layout.md)

## Getting Started
### API Examples

Examples using Fuchsia's Bluetooth Low Energy APIs for all four LE roles can be
found in Garnet and Topaz. All of these are currently compiled into Fuchsia by
default.

- __LE scanner__: see [`eddystone_agent`](https://fuchsia.googlesource.com/topaz/+/master/examples/eddystone_agent/).
This is a suggestion agent that proposes URL links that are obtained from
Eddystone beacons. This is built in topaz by default.
- __LE broadcaster__: see [`eddystone_advertiser`](https://fuchsia.googlesource.com/topaz/+/master/examples/bluetooth/eddystone_advertiser/).
This is a Flutter module that can advertise any entered URL as an Eddystone
beacon.
- __LE peripheral__: see the [`ble_rect`](https://fuchsia.googlesource.com/topaz/+/master/examples/bluetooth/ble_rect/)
and [`ble_battery_service`](../../examples/bluetooth/ble_battery_service) examples.
- __LE central__: see [`ble_scanner`](https://fuchsia.googlesource.com/topaz/+/master/examples/bluetooth/ble_scanner/).

### Control API

Dual-mode (LE + Classic) GAP operations that are typically exposed to privileged
clients are performed using the [control.fidl](../../public/fidl/fuchsia.bluetooth.control.fidl)
API. This API is intended for managing local adapters, device discovery & discoverability,
pairing/bonding, and other settings.

[`bt-cli`](tools/bt-cli) is a command-line front-end
for this API:

```
$ bt-cli
bluetooth> list-adapters
  Adapter 0
    id: bf004a8b-d691-4298-8c79-130b83e047a1
    address: 00:1A:7D:DA:0A
bluetooth>
```

We also have a Flutter [module](https://fuchsia.googlesource.com/docs/+/HEAD/glossary.md#module)
that acts as a Bluetooth system menu based on this API at
[topaz/app/bluetooth_settings](https://fuchsia.googlesource.com/topaz/+/master/app/bluetooth_settings/).

### Tools

See the [bluetooth/tools](tools/) package for more information on
available command line tools for testing/debugging.

### Running Tests

#### Unit tests
The `bluetooth_tests` package contains Bluetooth test binaries. This package is
defined in the [tests BUILD file](tests/BUILD.gn).

Host subsystem tests are compiled into a single [GoogleTest](https://github.com/google/googletest) binary,
which gets installed at `/system/test/bluetooth_unittests`.

##### Running on real hardware
* Run all the tests:
  ```
  $ runtests -t bt-host-unittests
  ```


* Or use the `--gtest_filter`
[flag](https://github.com/google/googletest/blob/master/googletest/docs/advanced.md#running-a-subset-of-the-tests) to run a subset of the tests:

  ```
  # This only runs the L2CAP unit tests.
  $ /pkgfs/packages/bluetooth_tests/0/test/bt-host-unittests --gtest_filter=L2CAP_*
  ```
  (We specify the full path in this case, because runtests doesn't allow us to pass through arbitrary arguments to the test binary.)


* And use the `--verbose` flag to set log verbosity:

  ```
  # This logs all messages logged using FXL_VLOG (up to level 2)
  $ /pkgfs/packages/bluetooth_tests/0/test/bt-host-unittests --verbose=2
  ```

##### Running on QEMU
If you don't have physical hardware available, you can run the tests in QEMU using the same commands as above. A couple of tips will help run the tests a little more quickly.

* Run the VM with hardware virtualization support: `fx run -k`
* Disable unnecessary logging for the tests:
  ```
  $ /pkgfs/packages/bluetooth_tests/0/test/bt-host-unittests --quiet=10
  ```

With these two tips, the full bt-host-unittests suite runs in ~2 seconds.

#### Integration Tests
TODO(armansito): Describe integration tests

### Controlling Log Verbosity

#### bin/bt-gap

The Bluetooth system service is invoked by sysmgr to resolve service requests.
The mapping between environment service names and their handlers is defined in
[bin/sysmgr/config/services.config](../../bin/sysmgr/config/services.config).
Add the `--verbose` option to the Bluetooth entries to increase verbosity, for
example:

```
...
    "bluetooth::control::AdapterManager": [ "bluetooth", "--verbose=2" ],
    "bluetooth::gatt::Server": [ "bluetooth", "--verbose=2" ],
    "bluetooth::low_energy::Central": [ "bluetooth", "--verbose=2" ],
    "bluetooth::low_energy::Peripheral": [ "bluetooth", "--verbose=2" ],
...

```

#### bthost

The bthost driver currently uses the FXL logging system. To enable maximum log
verbosity, set the `BT_DEBUG` macro to `1` in [drivers/bluetooth/host/driver.cc](../../drivers/bluetooth/host/driver.cc).
