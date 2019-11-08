# Dockyard Host

The `dockyard_host` is a host (developer machine) server that runs the Fuchsia
System Monitor Dockyard without a GUI. The initial development is primarily for
testing, though this could evolve into an independent host Dockyard.

To be effective, a dockyard needs a connection to a running harvester. The
harvester will transmit sample data to the dockyard for storage.

## To test

In one terminal window run
```
$ out/default/host_x64/dockyard_host
Starting dockyard host
Server listening on 0.0.0.0:50051
```

Determine the ip address of your host computer (e.g. `ifconfig`) and use it in
place of the "192.168.3.53" example used below.
In a second terminal window run
```
$ killall -r qemu-; fx qemu -N
<there will be a lot of output, after it calms down>
$ run fuchsia-pkg://fuchsia.com/system_monitor_harvester#meta/system_monitor_harvester.cmx 192.168.3.53:50051
```
Or
```
$ fx shell run \
    fuchsia-pkg://fuchsia.com/system_monitor_harvester#meta/system_monitor_harvester.cmx \
    192.168.3.53:50051
```

If the harvester is not able to connect to the dockyard_host, try using your
host's local IP instead of 192.168.3.53 (see `ifconfig` for your IP addresses).

See also: /docs/development/system_monitor/README.md
