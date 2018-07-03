This directory contains command-line tools for Bluetooth development.

## hcitool

`hcitool` uses the [host HCI library](../drivers/bluetooth/lib/hci) to send
HCI commands directly to a bt-hci device (`/dev/class/bt-hci/000` by default)

Currently all bt-hci devices are automatically claimed by the bt-host driver. To
use hcitool, disable the bt-host driver by passing `driver.bthost.disable` to
the kernel command line:

On host machine:
```
$ fx boot -- driver.bthost.disable
```

On Fuchsia:
```
$ hcitool reset
  Sent HCI_Reset (id=1)
  Command Complete - status 0x00 (id=1)
$ hcitool read-bdaddr
  Sent HCI_Read_BDADDR (id=1)
  Command Complete - status 0x00 (id=1)
  BD_ADDR: 00:1A:7D:DA:71:0A
```

## btsnoop

`btsnoop` uses the snoop channel of a specified bt-hci device (`/dev/class/bt-hci/000` by
default) and writes HCI traffic to a file (`/tmp/btsnoop.log` by default) supporting both [BTSnoop](http://www.fte.com/webhelp/bpa600/Content/Technical_Information/BT_Snoop_File_Format.htm)
and pcap formats. The captured packets can be visualized using any protocol analyzer that supports BTSnoop or pcap (e.g.
Wireshark).

The following command will sniff all HCI traffic to/from `/dev/class/bt-hci/001` and write it to
/tmp/btsnoop.log (on a Fuchsia device):

This will monitor `/dev/class/bt-hci/000` and output the traffic to `/tmp/btsnoop.log`:
```
$ btsnoop
```

To initiate a live capture using Wireshark (on host):

```
$ fx shell btsnoop --format pcap | wireshark -k -i -
```

To specify a custom HCI device and output location (on device):
```
$ btsnoop --path=/my/custom/path --dev=/dev/class/bt-hci/005
```

Logs can then be copied from the Fuchsia device and given to any tool that can parse
BTSnoop (e.g. Wireshark):
```
$ fx cp --to-host :/tmp/btsnoop.log ./
$ wireshark ./btsnoop.log
```

## bluetoothcli

`bluetoothcli` is a command-line interface for the Generic Access Profile (using
the [control](../../public/fidl/fuchsia.bluetooth.control.fidl) FIDL interfaces).
This can be used to query available Bluetooth controllers, to perform dual-mode
discovery and connection procedures, and to respond to pairing requests.

The `bluetooth` process does not need to be run directly before using
bluetoothcli, as it will be launched by sysmgr if necessary.

```
$ bluetoothcli
bluetooth> list-adapters
  Adapter 0
    id: bf004a8b-d691-4298-8c79-130b83e047a1
    address: 00:1A:7D:DA:0A
    powered: yes
```

## Other Tools

This package contains additional tools. Refer to each tool's own README for
more information.
