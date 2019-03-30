// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

const (
	DefaultNetwork   = "10.0.2.0/24"
	DefaultDHCPStart = "10.0.2.15"
	DefaultGateway   = "10.0.2.2"
	DefaultDNS       = "10.0.2.3"
)

const (
	TargetAArch64 = "aarch64"
	TargetX86_64  = "x86_64"
)

type Drive struct {
	// ID is the block device identifier.
	ID string

	// File is the disk image file.
	File string

	// Addr is the PCI address of the block device.
	Addr string
}

type Forward struct {
	// HostPort is the port on the host.
	HostPort int

	// GuestPort is the port on the guest.
	GuestPort int
}

type Netdev struct {
	// ID is the network device identifier.
	ID string

	// User is a netdev user backend.
	User *NetdevUser

	// Tap is a netdev tap backend.
	Tap *NetdevTap

	// MAC is the network device MAC address.
	MAC string
}

// NetdevUser defines a netdev backend giving user networking.
type NetdevUser struct {
	// Network is the network block.
	Network string

	// DHCPStart is the address at which the DHCP allocation starts.
	DHCPStart string

	// DNS is the address of the builtin DNS server.
	DNS string

	// Host is the host IP address.
	Host string

	// Forwards are the host forwardings.
	Forwards []Forward
}

// NetdevTap defines a netdev backend giving a tap interface.
type NetdevTap struct {
	// Name is the name of the interface.
	Name string
}

// Config gives a high-level configuration for QEMU on Fuchsia.
type Config struct {
	// QEMUBin is a path to the QEMU binary.
	Binary string

	// Target is the QEMU target (e.g., "x86_64" or "aarch64").
	Target string

	// CPU is the number of CPUs.
	CPU int

	// Memory is the amount of RAM.
	Memory int

	// KVM gives whether to enable KVM.
	KVM bool

	// Kernel is the path to the kernel image.
	Kernel string

	// Initrd is the path to the initrd image.
	Initrd string

	// Drives are drives to mount inside the QEMU instance.
	Drives []Drive

	// Networks are networks to set up inside the QEMU instance.
	Networks []Netdev
}

// CreateInvocation creates a QEMU invocation given a particular configuration, a list of
// images, and any specified command-line arguments.
func CreateInvocation(cfg Config, cmdlineArgs []string) ([]string, error) {
	if _, err := os.Stat(cfg.Binary); err != nil {
		return nil, fmt.Errorf("QEMU binary not found: %v", err)
	}
	absBinaryPath, err := filepath.Abs(cfg.Binary)
	if err != nil {
		return nil, err
	}

	invocation := []string{absBinaryPath}

	switch cfg.Target {
	case TargetAArch64:
		if cfg.KVM {
			invocation = append(invocation, "-machine", "virt,gic_version=host")
			invocation = append(invocation, "-cpu", "host")
			invocation = append(invocation, "-enable-kvm")
		} else {
			invocation = append(invocation, "-machine", "virt,gic_version=3")
			invocation = append(invocation, "-machine", "virtualization=true")
			invocation = append(invocation, "-cpu", "cortex-a53")
		}
	case TargetX86_64:
		invocation = append(invocation, "-machine", "q35")
		// TODO: this is Fuchsia specific, factor it out as another device struct.
		// Necessary for userboot.shutdown to trigger properly, since it writes
		// to 0xf4 to debug-exit in QEMU.
		invocation = append(invocation, "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04")
		if cfg.KVM {
			invocation = append(invocation, "-cpu", "host")
			invocation = append(invocation, "-enable-kvm")
		} else {
			invocation = append(invocation, "-cpu", "Haswell,+smap,-check,-fsgsbase")
		}
	default:
		return nil, fmt.Errorf("cpu %q not recognized", cfg.Target)
	}

	invocation = append(invocation, "-m", fmt.Sprintf("%d", cfg.Memory))
	invocation = append(invocation, "-smp", fmt.Sprintf("%d", cfg.CPU))
	invocation = append(invocation, "-nographic")
	invocation = append(invocation, "-serial", "stdio")
	invocation = append(invocation, "-monitor", "none")

	invocation = append(invocation, "-kernel", cfg.Kernel)
	invocation = append(invocation, "-initrd", cfg.Initrd)

	// TODO: maybe we should introduce Device interface with three different
	// implementations: Drive, Netdev and ISADebugExit to cleanup the code
	// below a bit.

	for _, d := range cfg.Drives {
		var drive strings.Builder
		fmt.Fprintf(&drive, "id=%s,file=%s,format=raw,if=none", d.ID, d.File)
		invocation = append(invocation, "-drive", drive.String())

		var device strings.Builder
		fmt.Fprintf(&device, "virtio-blk-pci,drive=%s", d.ID)
		if d.Addr != "" {
			fmt.Fprintf(&device, ",addr=%s", d.Addr)
		}
		invocation = append(invocation, "-device", device.String())
	}

	for _, n := range cfg.Networks {
		if n.ID == "" {
			return nil, fmt.Errorf("a network must have an ID")
		}

		var netdev strings.Builder
		if n.Tap != nil {
			if n.Tap.Name == "" {
				return nil, fmt.Errorf("network %q must specify a TAP interface name", n.ID)
			}
			// Overwrite any default configuration scripts with none; there is not currently a
			// good use case for these parameters.
			fmt.Fprintf(&netdev, "tap,id=%s,ifname=%s,script=no,downscript=no", n.ID, n.Tap.Name)
		} else if n.User != nil {
			fmt.Fprintf(&netdev, "user,id=%s", n.ID)
			if n.User.Network != "" {
				fmt.Fprintf(&netdev, ",net=%s", n.User.Network)
			}
			if n.User.DHCPStart != "" {
				fmt.Fprintf(&netdev, ",dhcpstart=%s", n.User.DHCPStart)
			}
			if n.User.DNS != "" {
				fmt.Fprintf(&netdev, ",dns=%s", n.User.DNS)
			}
			if n.User.Host != "" {
				fmt.Fprintf(&netdev, ",host=%s", n.User.Host)
			}
			for _, f := range n.User.Forwards {
				fmt.Fprintf(&netdev, ",hostfwd=tcp::%d-:%d", f.HostPort, f.GuestPort)
			}
		} else {
			return nil, fmt.Errorf("network %q must specify a netdev backend", n.ID)
		}
		invocation = append(invocation, "-netdev", netdev.String())

		var device strings.Builder
		fmt.Fprintf(&device, "virtio-net-pci,netdev=%s", n.ID)
		if n.MAC != "" {
			fmt.Fprintf(&device, ",mac=%s", n.MAC)
		}
		invocation = append(invocation, "-device", device.String())
	}
	// Treat the absense of specified networks as a directive to disable networking entirely.
	if len(cfg.Networks) == 0 {
		invocation = append(invocation, "-net", "none")
	}

	invocation = append(invocation, "-append", strings.Join(cmdlineArgs, " "))
	return invocation, nil
}
