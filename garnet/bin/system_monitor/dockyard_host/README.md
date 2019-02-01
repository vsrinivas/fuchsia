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
$ system_monitor_harvester 192.168.3.53:50051
harvester received: Hello world
```
If the harvester is not able to connect to the dockyard_host, try using your
host's local IP instead of 192.168.3.53 (see `ifconfig` for your IP addresses).

The message `Hello world` means that a connection and round-trip communication
was done (which all the dockyard_host and harvester do so far).

See also: ../README.md
