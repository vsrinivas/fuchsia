# Fuchsia Netstack Service

Netstack contains TCP/IP network stack and talks to network drivers.
It serves as a back-end for mxio socket API.

## Netstack is running by default

Netstack is automatically started by netsvc during system start-up.
You don't have to run it manually.
In this mode, netstack handles IPv4 packets only.

If you want netstack to handle IPv6 packets as well, you have to disable netsvc.
See [Start netstack manually](#Start-netstack-manually) for more details.

Netstack acquires an IPv4 address using DHCP.

## Building minimum user.bootfs image to test netstack

```
$ ./scripts/build-sysroot.sh
$ ./packages/gn/gen.py --modules netstack
$ ./buildtools/ninja -C out/debug-x86-64
```

## Using netstack on qemu

Create a tap interface called qemu if you haven't done so.
[This magenta document](https://fuchsia.googlesource.com/magenta/+/master/docs/qemu.md#Enabling-Networking-under-Qemu-x86_64-only) has instructions.

Choose an IPv4 subnet to use, for example, 192.168.3.0/24.
Assign 192.168.3.1 to the tap interface.

```
$ sudo ifconfig qemu 192.168.3.1/24
```

### Setting up dnsmasq

You need a DHCPv4 server running for the subnet.
In this document, we use dnsmasq.
On Ubuntu, install dnsmasq with apt-get.

```
$ sudo apt-get install dnsmasq
```
Add these 2 lines to /etc/dnsmasq.conf.

```
interface=qemu
dhcp-range=qemu,192.168.3.50,192.168.3.150,24h
```

Restart dnsmasq.

```
$ sudo /etc/init.d/dnsmasq restart
```

### Setting up NAT (on Linux)

Optionally you can set up NAT to route the traffic to the external network
interface (e.g. eth0)

```
echo 1 > /proc/sys/net/ipv4/ip_forward
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
iptables -A FORWARD -i eth0 -o qemu -m state --state RELATED,ESTABLISHED -j ACCEPT
iptables -A FORWARD -i qemu -o eth0 -j ACCEPT
```

### Running the qemu startup script with -N option

-N option will enable qemu's network feature

```
$ scripts/run-magenta-x86-64 -N -x out/debug-x86-64/user.bootfs
```

On the console, the IP address acquired from DHCP will be reported

```
[00004.917] U netstack: ip4_addr: 192.168.3.53
```

## Using netstack on NUC connecting to Linux Desktop

If you are connecting the Ethernet cable from NUC to a USB Ethernet adapter
on Linux Desktop, you can use a set-up similar to the qemu case.

Choose an IPv4 subnet to use, for example, 192.168.2.0/24.
Assign 192.168.2.1 to the USB Ethernet adapter (e.g. eth5)

```
$ sudo ifconfig eth5 192.168.2.1/24
```

Add these 2 lines to /etc/dnsmasq.

```
interface=eth5
dhcp-range=qemu,192.168.2.50,192.168.2.150,24h
```

## Start netstack manually

If you disable netsvc, netstack won't run automatically.
Use '-c netsvc.disable=true' option when you run the startup script.

```
$ scripts/run-magenta-x86-64 -N -x out/debug-x86-64/user.bootfs -c netsvc.disable=true
```

You can run netstack at the shell prompt.

```
> netstack&
```
