Bluetooth
=========

The Fuchsia Bluetooth stack is an implementation of the Bluetooth 5.0 Host Subsystem. It intends to
provide a dual-mode implementation of the Generic Access Profile, a module framework for developing
Bluetooth Low Energy applications, a limited set of Bluetooth Classic profiles, and a framework for
building HCI transport drivers.

## Packages

The
[bluetooth](https://fuchsia.googlesource.com/garnet/+/master/packages/bluetooth) package contains
the Bluetooth process, some command-line tools, and unit tests. The
[bluetooth_examples](https://fuchsia.googlesource.com/topaz/+/master/examples/bluetooth)
package contains various example applications that interact with the Bluetooth FIDL interfaces.

You generally do not need to build the entire Fuchsia tree for Bluetooth development. The following
configures a minimal build that includes Bluetooth unit tests and command-line tools (useful for
quick iteration times when developing unit tests on QEMU):

```
./packages/gn/gen.py --packages garnet/packages/fxl,garnet/packages/mtl,garnet/packages/bluetooth
```

*NOTE: You may see warnings related to skia which are safe to ignore. The warnings can be muted by
passing `--ignore-skia` to gen.py.*

To build the entire Fuchsia tree and also include the FIDL examples:

```
./packages/gn/gen.py --packages topaz/packages/examples,packages/gn/default
```

## Development

A `bt-hci` device is published for each local Bluetooth controller under `/dev/class/bt-hci/`. The
Bluetooth host-subsystem process and the command-line tools that interact with a Bluetooth
controller obtain
[channel](https://fuchsia.googlesource.com/zircon/+/master/docs/objects/channel.md) handles from a
bt-hci device using the ioctls defined
[here](https://fuchsia.googlesource.com/zircon/+/master/system/public/zircon/device/bt-hci.h).

### The bluetooth process

Bluetooth functionality is currently exposed via FIDL services that are provided by the Bluetooth
process (currently installed at `/system/pkgs/bluetooth`). This process implements the core
Bluetooth protocols required for the Generic Access Profile (HCI state machines, L2CAP, GATT,
SDP, SM, etc).

The process is launched by
[Bootstrap](https://fuchsia.googlesource.com/garnet/+/master/bin/bootstrap/) when an application
requests one of the Bluetooth services. The service-to-binary mapping for Bluetooth services is
currently defined in Bootstrap's
[`services.config`](https://fuchsia.googlesource.com/garnet/+/master/bin/bootstrap/services.config)
file.

[`bluetoothcli`](https://fuchsia.googlesource.com/garnet/+/master/bin/bluetooth_tools/bluetoothcli/) and
the [Flutter examples](examples) provide examples that interact with the Bluetooth services.

### Tools & Testing

#### bluetoothcli

This is a command-line client for the
[control](https://fuchsia.googlesource.com/garnet/+/master/public/lib/bluetooth/fidl/control.fidl) FIDL
interfaces. The `bluetooth` process does not need to launched directly before using `bluetoothcli`
since Bootstrap will launch a new `bluetooth` process during the service request if one isn't
already running.

```
$ bluetoothcli
bluetooth> list-adapters
  Adapter 0
    id: bf004a8b-d691-4298-8c79-130b83e047a1
    address: 00:1A:7D:DA:0A
    powered: yes
```

#### hcitool

`hcitool` can be used to directly send HCI commands to a bt-hci device:

```
$ hcitool reset
  Sent HCI_Reset (id=1)
  Command Complete - status 0x00 (id=1)
$ hcitool read-bdaddr
  Sent HCI_Read_BDADDR (id=1)
  Command Complete - status 0x00 (id=1)
  BD_ADDR: 00:1A:7D:DA:71:0A
```

hcitool binds the control channel of the specified bt-hci device (`/dev/class/bt-hci/000` by
default) provided that it has not already been bound by another process.

#### btsnoop

`btsnoop` binds the snoop channel of a specified bt-hci device (`/dev/class/bt-hci/000` by
default) and writes HCI traffic to a specified file using the [BTSnoop file
format](http://www.fte.com/webhelp/bpa600/Content/Technical_Information/BT_Snoop_File_Format.htm).
The captured packets can be visualized using any protocol analyzer that supports BTSnoop (e.g.
Wireshark).

The following command will sniff all HCI traffic to/from `/dev/class/bt-hci/001` and write it to
/tmp/btsnoop.log (on a Fuchsia device):
```
$ btsnoop --path=/tmp/btsnoop.log --dev=/dev/class/bt-hci/001
```

Logs can then be copied from the Fuchsia device and passed directly to Wireshark:
```
$ netcp :/tmp/btsnoop.log ./
$ wireshark ./btsnoop.log
```

#### bluetooth_unittests

All Bluetooth unit tests are currently compiled into a single GTest binary installed at
`/system/test/bluetooth_unittests`.

## Links

+ [Architecture](docs/architecture.md)
+ [Libraries](lib/README.md)
+ [FIDL Interfaces](service/interfaces)
+ [Tools](tools)
+ [Examples](examples)
