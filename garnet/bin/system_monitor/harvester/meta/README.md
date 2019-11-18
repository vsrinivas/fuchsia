See [package_metadata](/docs/concepts/storage/package_metadata) for documentation about this
directory.

For testing, start the component with (replace 192.168.42.10:50051 with the IP
address and port number for the dockyard):
```bash
$ run fuchsia-pkg://fuchsia.com/system_monitor_harvester#meta/system_monitor_harvester.cmx 192.168.42.10:50051 &
```

To check that it's running, try:
```bash
$ cs
```
