# Dockyard Host

The `dockyard_host` is a host (developer machine) server that runs the Fuchsia
System Monitor Dockyard without a GUI. The initial development is primarily for
testing, though this could evolve into an independent host Dockyard.

To be effective, a dockyard needs a connection to a running harvester. The
harvester will transmit sample data to the dockyard for storage.

## To test

In one terminal window run
```
$ out/x64/host_x64/dockyard_host
Starting dockyard host
Server listening on 0.0.0.0:50051
```

In a second terminal window run
```
$ killall -r qemu-; fx run -N -u $FUCHSIA_DIR/scripts/start-dhcp-server.sh
<there will be a lot of output, after it calms down>
$ run fuchsia-pkg://fuchsia.com/system_monitor_harvester#meta/system_monitor_harvester.cmx 192.168.3.53:50051
```

If the harvester is not able to connect to the dockyard_host, try using your
host's local IP instead of 192.168.3.53 (see `ifconfig` for your IP addresses).

See also: /docs/development/system_monitor/README.md
