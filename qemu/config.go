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

	// MAC is the network device MAC address.
	MAC string
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
		var netdev strings.Builder
		fmt.Fprintf(&netdev, "user,id=%s", n.ID)
		if n.Network != "" {
			fmt.Fprintf(&netdev, ",net=%s", n.Network)
		}
		if n.DHCPStart != "" {
			fmt.Fprintf(&netdev, ",dhcpstart=%s", n.DHCPStart)
		}
		if n.DNS != "" {
			fmt.Fprintf(&netdev, ",dns=%s", n.DNS)
		}
		if n.Host != "" {
			fmt.Fprintf(&netdev, ",host=%s", n.Host)
		}
		for _, f := range n.Forwards {
			fmt.Fprintf(&netdev, ",hostfwd=tcp::%d-:%d", f.HostPort, f.GuestPort)
		}
		invocation = append(invocation, "-netdev", netdev.String())

		var device strings.Builder
		fmt.Fprintf(&device, "virtio-net-pci,netdev=%s", n.ID)
		if n.MAC != "" {
			fmt.Fprintf(&device, ",mac=%s", n.MAC)
		}
		invocation = append(invocation, "-device", device.String())
	}

	invocation = append(invocation, "-append", strings.Join(cmdlineArgs, " "))
	return invocation, nil
}
