# Bluetooth

The Fuchsia Bluetooth system aims to provide a dual mode implementation of the
Bluetooth Host Subsystem (5.0+) supporting a framework for developing Low Energy
and Traditional profiles.

Source code shortcuts:

-   Public API:
    *   [Shared](/sdk/fidl/fuchsia.bluetooth)
    *   [System API](/sdk/fidl/fuchsia.bluetooth.sys)
    *   [BR/EDR (Profile)](/sdk/fidl/fuchsia.bluetooth.bredr)
    *   [GATT](/sdk/fidl/fuchsia.bluetooth.gatt)
    *   [LE](/sdk/fidl/fuchsia.bluetooth.le)
-   [Private API](/src/connectivity/bluetooth/fidl)
-   [Tools](tools/)
-   [Host Subsystem Driver](core/bt-host)
-   [HCI Drivers](hci)
-   [HCI Transport Drivers](hci/transport)

For more orientation, see

-   [System Architecture](/docs/concepts/bluetooth/architecture.md)
-   [Respectful Code](#Respectful-Code)

For a note on used (and avoided) vocabulary, see
[Bluetooth Vocabulary](docs/vocabulary.md)

## Getting Started

### API Examples

Examples using Fuchsia's Bluetooth Low Energy APIs can be found
[here](examples).

### Privileged System API

Dual-mode (LE + Classic) GAP operations that are typically exposed to privileged
clients are performed using the
[fuchsia.bluetooth.sys](/sdk/fidl/fuchsia.bluetooth.sys) library. This API is
intended for managing local adapters, device discovery & discoverability,
pairing/bonding, and other settings.

[`bt-cli`](tools/bt-cli) is a command-line front-end for privileged access
operations:

```
$ bt-cli
bt> list-adapters
Adapter:
    Identifier:     e5878e9f642d8908
    Address:        34:13:E8:86:8C:19
    Technology:     DualMode
    Local Name:     siren-relic-wad-pout
    Discoverable:   false
    Discovering:    false
    Local UUIDs:    None
```

### Tools

See the [bluetooth/tools](tools/) package for more information on available
command line tools for testing/debugging.

### Running Tests

Your build configuration may or may not include Bluetooth tests. Ensure
Bluetooth tests are built and installed when paving or OTA'ing with
[`fx set`](/docs/development/build/fx.md#configure-a-build):

```
  $ fx set workstation.x64 --with //src/connectivity/bluetooth,//bundles:tools
```

#### Tests

The Bluetooth codebase follows the
[Fuchsia testing best practices](/docs/contribute/testing/best-practices.md). In
general, the Bluetooth codebase defines an associated unit test binary for each
production binary and library, as well as a number of integration test binaries.
Without good reason, no code should be added without an appropriate (unit,
integration, etc) corresponding test. Look in the `GN` file of a production
binary or library to find its associated unit tests.

For more information, see the
[Fuchsia testing guide](docs/development/testing/run_fuchsia_tests.md).

##### Running on a Fuchsia device

*   Run all the bt-host unit tests:

    ```
    $ fx test //src/connectivity/bluetooth/core/bt-host
    ```

*   Run a specific test within `bt-host`:

    ```
    $ fx test //src/connectivity/bluetooth/core/bt-host -- --gtest_filter='Foo.Bar'
    ```

    Where `Foo` and `Bar` in `Foo.Bar` are the fixture name and the test name,
    respectively.

To see all options for running these tests, run `fx test --help`.

##### Running on the Fuchsia Emulator

If you don't have physical hardware available, you can run the tests in the
Fuchsia emulator (FEMU) using the same commands as above. See
[FEMU set up instructions](/docs/get-started/set_up_femu.md).

#### Integration Tests

See the [Integration Test README](tests/integration/README.md)

### Controlling Log Verbosity

#### Logging in Drivers

The most reliable way to enable higher log verbosity is with kernel command line
parameters. These can be configured through the `fx set` command:

```
  fx set workstation.x64 --args="dev_bootfs_labels=[\"//src/connectivity/bluetooth:driver-debug-logging\"]"
```

This will enable debug-level logging for all supported chipsets. Using `fx set`
writes these values into the image, so they will survive a restart. For more
detail on driver logging, see
[Zircon driver logging](/docs/concepts/drivers/driver-development.md#logging)

#### `core/bt-gap`

The Bluetooth system service is invoked by sysmgr to resolve service requests.
The mapping between environment service names and their handlers is defined in
[//src/sys/sysmgr/config/services.config](/src/sys/sysmgr/config/services.config).

### Inspecting Component State

The Bluetooth system supports inspection through the
[Inspect API](/docs/development/diagnostics/inspect). bt-gap, bt-host, bt-a2dp,
and bt-snoop all expose information though Inspect.

#### Usage

*   bt-host: `fx iquery show-file /dev/diagnostics/class/bt-host/000.inspect`
    exposes information about the controller, peers, and services.
*   bt-gap: `fx iquery show bt-gap` exposes information on host devices managed
    by bt-gap, pairing capabilities, stored bonds, and actively connected peers.
*   bt-a2dp: `fx iquery show bt-a2dp` exposes information on audio streaming
    capabilities and active streams
*   bt-snoop: `fx iquery show bt-snoop` exposes information about which hci
    devices are being logged and how much data is stored.
*   All Bluetooth components: `fx iquery show bt-*`

See the [iquery documentation](/docs/development/diagnostics/inspect/iquery) for
complete instructions on using `iquery`.

### Respectful Code

Inclusivity is central to Fuchsia's culture, and our values include treating
each other with dignity. As such, it’s important that everyone can contribute
without facing the harmful effects of bias and discrimination.

Bluetooth Core Specification 5.3 updated certain terms that were identified as
inappropriate to more inclusive versions. For example, usages of 'master' and
'slave' were changed to 'central' and 'peripheral', respectively. We have
transitioned our code's terminology to the more appropriate language. We no
longer allow uses of the prior terms. For more information, see the
[Appropriate Language Mapping Table](https://specificationrefs.bluetooth.com/language-mapping/Appropriate_Language_Mapping_Table.pdf)
published by the Bluetooth SIG.

See the Fuchsia project [guide](/docs/best-practices/respectful_code.md) on best
practices for more information.
