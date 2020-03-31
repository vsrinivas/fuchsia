# Testing against a local DHCP server

These instructions will run a DHCP server locally on your computer for testing
the DHCP client.

Run qemu with Fuchsia. We'll give Fuchsia two NICs so that we can test the
interaction between them and DHCP. Make all the tunnels:

    sudo tunctl -u $USER -t qemu
    sudo tunctl -u $USER -t qemu-extra
    sudo ifconfig qemu up
    sudo ifconfig qemu-extra up

Now build and run Fuchsia with those two ports:

    fx build && fx qemu -kN -- -nic tap,ifname=qemu-extra,model=e1000

Now install a DHCP server.

    sudo apt-get install isc-dhcp-server
    ps aux | grep dhcp

If dhcpd was started as part of that, kill it:

    sudo systemctl disable isc-dhcp-server.service

Make a file called /tmp/dhcpd.conf with these contents:

    default-lease-time 3600;
    max-lease-time 7200;
    authoritative;
    subnet 172.18.0.0 netmask 255.255.0.0 {
      option routers                  172.18.0.1;
      option subnet-mask              255.255.0.0;
      option domain-search            "testdomain18.lan";
      option domain-name-servers      172.18.0.1;
      range   172.18.0.10   172.18.0.100;
    }
    subnet 172.19.0.0 netmask 255.255.0.0 {
      option routers                  172.19.0.1;
      option subnet-mask              255.255.0.0;
      option domain-search            "testdomain19.lan";
      option domain-name-servers      172.19.0.1;
      range   172.19.0.10   172.19.0.100;
    }

dhcpd knows which addresses to serve on which nics based on matching the
existing IP address on the NIC so you need to set those:

    sudo ifconfig qemu 172.18.0.1
    sudo ifconfig qemu-extra 172.19.0.1

Now we'll run dhcpd.  You can use the filenames below or modify them.

If the leases file doesn't already exist, dhcpd might complain.  Go ahead and
create the file first:

    sudo touch /tmp/dhcpd.leases

Now run:

    sudo dhcpd -4 -f -d -cf /tmp/dhcpd.conf -lf /tmp/dhcpd.leases qemu qemu-extra

Now Fuchsia should get DHCP addresses and you'll see debug output for each step
in the dhcpd output.
