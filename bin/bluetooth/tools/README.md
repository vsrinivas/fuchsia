This directory contains command-line tools for Bluetooth development.

## bt-hci-tool

`bt-hci-tool` uses the [host HCI library](../drivers/bluetooth/lib/hci) to send
HCI commands directly to a bt-hci device (`/dev/class/bt-hci/000` by default)

Currently all bt-hci devices are automatically claimed by the bt-host driver. To
use bt-hci-tool, disable the bt-host driver by passing `driver.bthost.disable`
to the kernel command line:

On host machine:
```
$ fx boot -- -- driver.bthost.disable
```

On Fuchsia:
```
$ bt-hci-tool reset
  Sent HCI_Reset (id=1)
  Command Complete - status 0x00 (id=1)
$ bt-hci-tool read-bdaddr
  Sent HCI_Read_BDADDR (id=1)
  Command Complete - status 0x00 (id=1)
  BD_ADDR: 00:1A:7D:DA:71:0A
```

## bt-snoop

`bt-snoop` uses the snoop channel of a specified bt-hci device
(`/dev/class/bt-hci/000` by default) and writes HCI traffic to a file (stdout by
default) supporting both
[BTSnoop](http://www.fte.com/webhelp/bpa600/Content/Technical_Information/BT_Snoop_File_Format.htm)
and pcap formats. The captured packets can be visualized using any protocol
analyzer that supports BTSnoop or pcap (e.g. Wireshark).

This will monitor `/dev/class/bt-hci/000` and output the traffic to stdout:

```
$ bt-snoop
```

To initiate a live capture using Wireshark (on host):

```
$ fx shell bt-snoop --format=pcap | wireshark -k -i -
```

To specify a custom HCI device and output location (on device):
```
$ bt-snoop --output=/my/custom/path --device=/dev/class/bt-hci/005
```

Logs can then be copied from the Fuchsia device and given to any tool that can
parse BTSnoop (e.g. Wireshark):
```
$ fx cp --to-host :/tmp/btsnoop.log ./
$ wireshark ./btsnoop.log
```

## bt-cli

`bt-cli` is a command-line interface for the Generic Access Profile (using the
[control](../../public/fidl/fuchsia.bluetooth.control.fidl) FIDL interfaces).
This can be used to query available Bluetooth controllers, to perform dual-mode
discovery and connection procedures, and to respond to pairing requests.

The `bluetooth` process does not need to be run directly before using
bt-cli, as it will be launched by sysmgr if necessary.

```
$ bt-cli
bt> list-adapters
Adapter:
        Identifier:     35700c9b-6748-4676-bd2c-1d863fd89210
        Address:        28:C6:3F:2F:D4:14
        Technology:     DualMode
        Local Name:     fuchsia fuchsia-unset-device-name
        Discoverable:   false
        Discovering:    false
        Local UUIDs:    None
```

## Other Tools

This package contains additional tools. Refer to each tool's own README for
more information.
