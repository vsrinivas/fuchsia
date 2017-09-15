# Running multiple QEMU instances w/ network

Running Fuchsia on multiple instances of QEMU on the same machine with network
is useful for developing multi-device features, especially with ledger
synchronization between them.

## Instructions for running two QEMU instances w/ network and ledger

These instructions are for linux hosts only. Running one Qemu on MacOS is slow
enough so it's probably useless to try to run two.

There should be a way to just use
the
[start-dhcp-server.sh](https://fuchsia.googlesource.com/scripts/+/master/start-dhcp-server.sh) twice,
but I (mesch) could not get it to work. The difficulty is that each dnsmasq
instance, even if it's confined to serve DHCP on only one interface, still
serves DNS on *all* interfaces.

We also set up persistent storage for the ledger. Notice, however, that with
ledger sync state is preserved in the cloud sync location for the ledger, so
it's usually not lost when the device is rebooted. The only thing persitent
storage buys is to preserve the ledger cloud sync configuration.

### Prerequisites on host

1. Create two [tap], `qemu0` and `qemu1` (both are separate from the default
value `qemu`, to distiguish the qemu instances started here). The values are
passed to the `-I` command line option below:

```
sudo tunctl -u $USER -t qemu0
sudo tunctl -u $USER -t qemu1
```

2. Choose two *different* IPv4 subnets to use for `qemu0` and `qemu1`, say
`192.168.7.0` and `192.168.8.0` (both are separate from the default for either
qemu or acer, for the same reason as above):

```
sudo ifconfig qemu0 192.168.7.1 up
sudo ifconfig qemu1 192.168.8.1 up
```

3. Configure dnsmasq to serve DHCP on these two devices. Add this to
`/etc/dnsmasq.conf`:

```
interface=qemu0
dhcp-range=qemu0,192.168.7.50,192.168.7.150,24h

interface=qemu1
dhcp-range=qemu1,192.168.8.50,192.168.8.150,24h
```

4. Restart `dnsmasq` to pick up the configuration change:

```
sudo /etc/init.d/dnsmasq stop
sudo /etc/init.d/dnsmasq start
```


5. Setup NAT:

```
echo 1 > /proc/sys/net/ipv4/ip_forward
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
iptables -A FORWARD -i eth0 -o qemu0 -m state --state RELATED,ESTABLISHED -j ACCEPT
iptables -A FORWARD -i qemu0 -o eth0 -j ACCEPT
iptables -A FORWARD -i eth0 -o qemu1 -m state --state RELATED,ESTABLISHED -j ACCEPT
iptables -A FORWARD -i qemu1 -o eth0 -j ACCEPT
```

6. Create two [minfs] block devices one for each instance. Call them `blk0.bin`
and `blk1.bin`.

### Command line on host

Start two qemu instances running fuchsia/zircon.

```
$ scripts/run-zircon-x86-64 -N -I qemu0 -x out/debug-x86-64/user.bootfs -g -- -hda blk0.bin
$ scripts/run-zircon-x86-64 -N -I qemu1 -x out/debug-x86-64/user.bootfs -g -- -hda blk1.bin
```

### Prerequisites on fuchsia

Each fuchsia instance needs to be set up once:

1. For the [minfs] block devices create partitions and filesystems and configure
them to be mounted to boot.

2. Configure [cloudsync] to point to the same cloud sync location.


### Command line on fuchsia

To run one story and watch it sync between devices, for example the
`example_todo_story`, do something like this:

1. Start a todo story on one side:

```
device_runner --user_shell=dev_user_shell --user_shell_args=--root_module=example_todo_story

```

2. Retrieve the story id from the log output, then start the same story on the other side:

```
device_runner --user_shell=dev_user_shell --user_shell_args='--story_id=CmfpuRWuBo'

```

You can also just run the default user shell and restart stories from the timeline.

[qemu]: https://fuchsia.googlesource.com/zircon/+/master/docs/qemu.md "QEMU"
[fuchsia]: https://fuchsia.googlesource.com/docs/+/HEAD/getting_started.md#Enabling-Network "Fuchsia Network"
[tap]: https://fuchsia.googlesource.com/zircon/+/master/docs/qemu.md#Enabling-Networking-under-QEMU-x86_64-only "TAP interfaces"
[cloudsync]: https://fuchsia.googlesource.com/ledger/+/HEAD/docs/user_guide.md#Cloud-Sync "Cloud Sync in Ledger"
[minfs]: https://fuchsia.googlesource.com/zircon/+/master/docs/minfs.md
