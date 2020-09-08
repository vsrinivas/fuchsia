// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
	"fmt"
	"strings"
)

const (
	DefaultNetwork   = "10.0.2.0/24"
	DefaultDHCPStart = "10.0.2.15"
	DefaultGateway   = "10.0.2.2"
	DefaultDNS       = "10.0.2.3"
)

type Drive struct {
	// ID is the block device identifier.
	ID string

	// File is the disk image file.
	File string

	// Addr is the PCI address of the block device.
	Addr string
}

type Chardev struct {
	// ID is the character device identifier.
	ID string

	// Logfile is a path to write the output to.
	Logfile string

	// Signal controls whether signals are enabled on the terminal.
	Signal bool
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

// Target is a QEMU architecture target.
type Target string

type targetList struct {
	AArch64 Target
	X86_64  Target
}

// TargetEnum provides accessors to valid QEMU target strings.
var TargetEnum = &targetList{
	AArch64: "aarch64",
	X86_64:  "x86_64",
}

// QEMUCommandBuilder provides methods to construct an arbitrary
// QEMU invocation, it does not validate inputs only that the
// resulting invocation is structurely valid.
type QEMUCommandBuilder struct {
	args       []string
	qemuPath   string
	hasNetwork bool
	initrd     string
	kernel     string
	kernelArgs []string
}

func (q *QEMUCommandBuilder) SetFlag(args ...string) {
	q.args = append(q.args, args...)
}

func (q *QEMUCommandBuilder) SetBinary(qemuPath string) {
	q.qemuPath = qemuPath
}

func (q *QEMUCommandBuilder) SetKernel(kernel string) {
	q.kernel = kernel
}

func (q *QEMUCommandBuilder) SetInitrd(initrd string) {
	q.initrd = initrd
}

func (q *QEMUCommandBuilder) SetTarget(target Target, kvm bool) {
	switch target {
	case TargetEnum.AArch64:
		if kvm {
			q.SetFlag("-machine", "virt-2.12,gic-version=host")
			q.SetFlag("-cpu", "host")
			q.SetFlag("-enable-kvm")
		} else {
			q.SetFlag("-machine", "virt-2.12,gic-version=3,virtualization=true")
			q.SetFlag("-cpu", "max")
		}
	case TargetEnum.X86_64:
		q.SetFlag("-machine", "q35")
		if kvm {
			q.SetFlag("-cpu", "host")
			q.SetFlag("-enable-kvm")
		} else {
			q.SetFlag("-cpu", "Haswell,+smap,-check,-fsgsbase")
		}
	}
}

func (q *QEMUCommandBuilder) SetMemory(memoryBytes int) {
	q.SetFlag("-m", fmt.Sprintf("%d", memoryBytes))
}

func (q *QEMUCommandBuilder) SetCPUCount(cpuCount int) {
	q.SetFlag("-smp", fmt.Sprintf("%d", cpuCount))
}

func (q *QEMUCommandBuilder) AddVirtioBlkPciDrive(d Drive) {
	iothread := fmt.Sprintf("iothread-%s", d.ID)
	q.SetFlag("-object", fmt.Sprintf("iothread,id=%s", iothread))
	q.SetFlag("-drive", fmt.Sprintf("id=%s,file=%s,format=raw,if=none,cache=unsafe,aio=threads", d.ID, d.File))
	device := fmt.Sprintf("virtio-blk-pci,drive=%s,iothread=%s", d.ID, iothread)
	if d.Addr != "" {
		device += fmt.Sprintf(",addr=%s", d.Addr)
	}
	q.SetFlag("-device", device)
}

func (q *QEMUCommandBuilder) AddSerial(c Chardev) {
	var builder strings.Builder
	builder.WriteString(fmt.Sprintf("stdio,id=%s", c.ID))
	if c.Logfile != "" {
		builder.WriteString(fmt.Sprintf(",logfile=%s", c.Logfile))
	}
	if !c.Signal {
		builder.WriteString(",signal=off")
	}
	q.SetFlag("-chardev", builder.String())
	device := fmt.Sprintf("chardev:%s", c.ID)
	q.SetFlag("-serial", device)
}

func (q *QEMUCommandBuilder) AddNetwork(n Netdev) {
	var network strings.Builder
	if n.Tap != nil {
		// Overwrite any default configuration scripts with none; there is not currently a
		// good use case for these parameters.
		network.WriteString(fmt.Sprintf("tap,id=%s,ifname=%s,script=no,downscript=no", n.ID, n.Tap.Name))
	} else if n.User != nil {
		network.WriteString(fmt.Sprintf("user,id=%s", n.ID))
		if n.User.Network != "" {
			network.WriteString(fmt.Sprintf(",net=%s", n.User.Network))
		}
		if n.User.DHCPStart != "" {
			network.WriteString(fmt.Sprintf(",dhcpstart=%s", n.User.DHCPStart))
		}
		if n.User.DNS != "" {
			network.WriteString(fmt.Sprintf(",dns=%s", n.User.DNS))
		}
		if n.User.Host != "" {
			network.WriteString(fmt.Sprintf(",host=%s", n.User.Host))
		}
		for _, f := range n.User.Forwards {
			network.WriteString(fmt.Sprintf(",hostfwd=tcp::%d-:%d", f.HostPort, f.GuestPort))
		}
	}
	q.SetFlag("-netdev", network.String())

	device := fmt.Sprintf("virtio-net-pci,netdev=%s", n.ID)
	if n.MAC != "" {
		device += fmt.Sprintf(",mac=%s", n.MAC)
	}
	q.SetFlag("-device", device)

	q.hasNetwork = true
}

func (q *QEMUCommandBuilder) AddKernelArg(kernelArg string) {
	q.kernelArgs = append(q.kernelArgs, kernelArg)
}

func (q *QEMUCommandBuilder) validate() error {
	if q.qemuPath == "" {
		return fmt.Errorf("QEMU binary path must be set.")
	}
	if q.kernel == "" {
		return fmt.Errorf("QEMU kernel path must be set.")
	}
	if q.initrd == "" {
		return fmt.Errorf("QEMU initrd path must be set.")
	}
	return nil
}

// Build creates a QEMU invocation given a particular configuration, a list of
// images, and any specified command-line arguments.
func (q *QEMUCommandBuilder) Build() ([]string, error) {
	if err := q.validate(); err != nil {
		return []string{}, err
	}
	cmd := []string{
		q.qemuPath,
		"-kernel", q.kernel,
		"-initrd", q.initrd,
	}

	cmd = append(cmd, q.args...)

	// Treat the absense of specified networks as a directive to disable networking entirely.
	if !q.hasNetwork {
		cmd = append(cmd, "-net", "none")
	}

	if len(q.kernelArgs) > 0 {
		cmd = append(cmd, "-append", strings.Join(q.kernelArgs, " "))
	}

	return cmd, nil
}
