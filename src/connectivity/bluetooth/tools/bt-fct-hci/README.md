Raw HCI factory testing tool.

## bt-fct-hci

Utility to send raw HCI test commands to the hardware on the command channel.

This utility opens the bt-hci device's command channel, sends the passed in HCI
command, and waits until either an error, event, or expected result code is
returned.

The command channel of a bt-hci device must be unclaimed for this tool to
function as intended. The bt-host driver is a common reason for the HCI
endpoints to be unavailable. If the bt-host driver isn't disabled or excluded
in the image, it can be disabled by passing `driver.bt_host.disable` to the
kernel command-line.

On the host machine while configuring set options add:
```
$ fx set --args=dev_bootfs_labels+=\[\"//src/connectivity/bluetooth/tools:disable-bt-host\"\] ...
```
