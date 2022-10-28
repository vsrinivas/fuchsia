This directory contains command-line tools for Bluetooth development.

Note: to use the bluetooth tools with your Fuchsia build, add `--with //src/connectivity/bluetooth/tools` to your `fx set` command.

## bt-hci-tool

`bt-hci-tool` uses the [host HCI library](../../../drivers/bluetooth/lib/hci) to send
HCI commands directly to a bt-hci device (`/dev/class/bt-hci/000` by default)

Currently all bt-hci devices are automatically claimed by the bt-host driver. To use bt-hci-tool,
disable the bt-host driver, which is done via the
[driver.<driver_name>.disable kernel cmdline flag](https://fuchsia.dev/fuchsia-src/reference/kernel/kernel_cmdline?hl=en#drivernamedisable).

This can be done at build-time by modifying your `fx set` to:
```
fx set <whatever else you put here> --args=dev_bootfs_labels=\[\"//src/connectivity/bluetooth:disable-bt-host\"\]
```
or by adding:
```
dev_bootfs_labels = [ "//src/connectivity/bluetooth:disable-bt-host" ]
```
to your `fx args`.

The build-time method is recommended, but you may also be able to do so by passing
`driver.bt_host.disable` to the kernel command line at pave time (not tested):

On host machine:
```
$ fx pave -- driver.bt_host.disable
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

## bt-snoop-cli

`bt-snoop-cli` is a command line client of the `bt-snoop` service. `bt-snoop` monitors snoop
channels for all bluetooth adapters on the system.
`bt-snoop-cli` subscribes to snoop logs for HCI devices and writes traffic to a file (stdout by
default) supporting the pcap format. The captured packets can be visualized using any protocol
analyzer that supports pcap (e.g. Wireshark).

This will fetch the current buffer of packets for all devices under `/dev/class/bt-hci/`,
output the traffic to stdout, then exit:

```
$ bt-snoop-cli --dump --format pretty
```

To initiate a live capture using Wireshark (on host):

```
$ fx shell bt-snoop-cli | wireshark -k -i -
```

To specify a custom HCI device ("005") and output location (on device):
```
$ bt-snoop-cli --output /my/custom/path --device 005
```

Logs can then be copied from the Fuchsia device and given to any tool that can
parse BTSnoop (e.g. Wireshark):
```
$ fx cp --to-host :/tmp/btsnoop.log ./
$ wireshark ./btsnoop.log
```

See the tool's help for complete usage:
```
$ bt-snoop-cli --help
```

## bt-cli

`bt-cli` is a command-line interface for the Generic Access Profile (using the
[fuchsia.bluetooth.sys](/sdk/fidl/fuchsia.bluetooth.sys) FIDL interfaces).
This can be used to query available Bluetooth controllers, to perform dual-mode
discovery and connection procedures, and to respond to pairing requests.

The `bluetooth` process does not need to be run directly before using
bt-cli, as it will be launched by sysmgr if necessary.

```
$ bt-cli
bt> adapter
Adapter:
        identifier:     35700c9b-6748-4676-bd2c-1d863fd89210
        addresses:      [address (public) 28:C6:3F:2F:D4:14]
                        [address (random) 28:C6:3F:2F:D4:15]
        active:         true
        technology:     DualMode
        local name:     fuchsia fuchsia-unset-device-name
        discoverable:   false
        discovering:    false
```

## Other Tools

This package contains additional tools. Refer to each tool's own README for
more information.
