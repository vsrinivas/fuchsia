// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package target

import (
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/tools/bootserver/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/qemu"
)

const (
	// qemuSystemPrefix is the prefix of the QEMU binary name, which is of the
	// form qemu-system-<QEMU arch suffix>.
	qemuSystemPrefix = "qemu-system"

	// DefaultInterfaceName is the name given to the emulated tap interface.
	defaultInterfaceName = "qemu"

	// DefaultMACAddr is the default MAC address given to a QEMU target.
	defaultMACAddr = "52:54:00:63:5e:7a"

	// DefaultNodename is the default nodename given to an target with the default QEMU MAC address.
	defaultNodename = "step-atom-yard-juicy"
)

// qemuTargetMapping maps the Fuchsia target name to the name recognized by QEMU.
var qemuTargetMapping = map[string]string{
	"x64":   qemu.TargetX86_64,
	"arm64": qemu.TargetAArch64,
}

// MinFS is the configuration for the MinFS filesystem image.
type MinFS struct {
	// Image is the path to the filesystem image.
	Image string `json:"image"`

	// PCIAddress is the PCI address to map the device at.
	PCIAddress string `json:"pci_address"`
}

// QEMUConfig is a QEMU configuration.
type QEMUConfig struct {
	// Path is a path to a directory that contains QEMU system binary.
	Path string `json:"path"`

	// Target is the QEMU target to emulate.
	Target string `json:"target"`

	// CPU is the number of processors to emulate.
	CPU int `json:"cpu"`

	// Memory is the amount of memory (in MB) to provide.
	Memory int `json:"memory"`

	// KVM specifies whether to enable hardware virtualization acceleration.
	KVM bool `json:"kvm"`

	// Whether User networking is enabled; if false, a Tap interface will be used.
	UserNetworking bool `json:"user_networking"`

	// MinFS is the filesystem to mount as a device.
	MinFS *MinFS `json:"minfs,omitempty"`
}

// QEMUTarget is a QEMU target.
type QEMUTarget struct {
	config  QEMUConfig
	opts    Options
	c       chan error
	process *os.Process
}

// NewQEMUTarget returns a new QEMU target with a given configuration.
func NewQEMUTarget(config QEMUConfig, opts Options) *QEMUTarget {
	return &QEMUTarget{
		config: config,
		opts:   opts,
		c:      make(chan error),
	}
}

// Nodename returns the name of the target node.
func (t *QEMUTarget) Nodename() string {
	return defaultNodename
}

// IPv4Addr returns a nil address, as DHCP is not currently configured.
func (t *QEMUTarget) IPv4Addr() (net.IP, error) {
	return nil, nil
}

// Serial returns the serial device associated with the target for serial i/o.
func (t *QEMUTarget) Serial() io.ReadWriteCloser {
	return nil
}

// SSHKey returns the private SSH key path associated with the authorized key to be pavet.
func (t *QEMUTarget) SSHKey() string {
	return t.opts.SSHKey
}

// Start starts the QEMU target.
func (t *QEMUTarget) Start(ctx context.Context, images []bootserver.Image, args []string) error {
	if t.process != nil {
		return fmt.Errorf("a process has already been started with PID %d", t.process.Pid)
	}

	// The QEMU command needs to be invoked within an empty directory, as QEMU
	// will attempt to pick up files from its working directory, one notable
	// culprit being multiboot.bin.  This can result in strange behavior.
	workdir, err := ioutil.TempDir("", "qemu-working-dir")
	if err != nil {
		return err
	}

	qemuTarget, ok := qemuTargetMapping[t.config.Target]
	if !ok {
		return fmt.Errorf("invalid target %q", t.config.Target)
	}

	if t.config.Path == "" {
		return fmt.Errorf("directory must be set")
	}
	qemuSystem := filepath.Join(t.config.Path, fmt.Sprintf("%s-%s", qemuSystemPrefix, qemuTarget))
	if _, err := os.Stat(qemuSystem); err != nil {
		return fmt.Errorf("could not find qemu-system binary %q: %v", qemuSystem, err)
	}

	var qemuKernel, zirconA, storageFull bootserver.Image
	for _, img := range images {
		switch img.Name {
		case "qemu-kernel":
			qemuKernel = img
		case "zircon-a":
			zirconA = img
		case "storage-full":
			storageFull = img
		}
	}
	if qemuKernel.Reader == nil {
		return fmt.Errorf("could not find qemu-kernel")
	}
	if zirconA.Reader == nil {
		return fmt.Errorf("could not find zircon-a")
	}

	if err := copyImagesToDir(workdir, &qemuKernel, &zirconA, &storageFull); err != nil {
		return err
	}

	var drives []qemu.Drive
	if storageFull.Reader != nil {
		drives = append(drives, qemu.Drive{
			ID:   "maindisk",
			File: filepath.Join(workdir, storageFull.Name),
		})
	}
	if t.config.MinFS != nil {
		if _, err := os.Stat(t.config.MinFS.Image); err != nil {
			return fmt.Errorf("could not find minfs image %q: %v", t.config.MinFS.Image, err)
		}
		file, err := filepath.Abs(t.config.MinFS.Image)
		if err != nil {
			return err
		}
		// Swarming hard-links Isolate downloads with a cache and the very same
		// cached minfs image will be used across multiple tasks. To ensure
		// that it remains blank, we must break its link.
		if err := overwriteFileWithCopy(file); err != nil {
			return err
		}
		drives = append(drives, qemu.Drive{
			ID:   "testdisk",
			File: file,
			Addr: t.config.MinFS.PCIAddress,
		})
	}

	netdev := qemu.Netdev{
		ID:  "net0",
		MAC: defaultMACAddr,
	}
	if t.config.UserNetworking {
		netdev.User = &qemu.NetdevUser{}
	} else {
		netdev.Tap = &qemu.NetdevTap{
			Name: defaultInterfaceName,
		}
	}
	networks := []qemu.Netdev{netdev}

	config := qemu.Config{
		Binary:   qemuSystem,
		Target:   qemuTarget,
		CPU:      t.config.CPU,
		Memory:   t.config.Memory,
		KVM:      t.config.KVM,
		Kernel:   filepath.Join(workdir, qemuKernel.Name),
		Initrd:   filepath.Join(workdir, zirconA.Name),
		Drives:   drives,
		Networks: networks,
	}

	// The system will halt on a kernel panic instead of rebooting.
	args = append(args, "kernel.halt-on-panic=true")
	// Print a message if `dm poweroff` times out.
	args = append(args, "devmgr.suspend-timeout-debug=true")
	// Do not print colors.
	args = append(args, "TERM=dumb")
	if t.config.Target == "x64" {
		// Necessary to redirect to stdout.
		args = append(args, "kernel.serial=legacy")
	}

	invocation, err := qemu.CreateInvocation(config, args)
	if err != nil {
		return err
	}

	cmd := &exec.Cmd{
		Path:   invocation[0],
		Args:   invocation,
		Dir:    workdir,
		Stdout: os.Stdout,
		Stderr: os.Stderr,
	}
	log.Printf("QEMU invocation:\n%s", strings.Join(invocation, " "))

	if err := cmd.Start(); err != nil {
		os.RemoveAll(workdir)
		return fmt.Errorf("failed to start: %v", err)
	}
	t.process = cmd.Process

	// Ensure that the working directory when QEMU finishes whether the Wait
	// method is invoked or not.
	go func() {
		t.c <- qemu.CheckExitCode(cmd.Wait())
		os.RemoveAll(workdir)
	}()

	return nil
}

// Restart stops the QEMU target and starts it again.
func (t *QEMUTarget) Restart(context.Context) error {
	return ErrUnimplemented
}

// Stop stops the QEMU target.
func (t *QEMUTarget) Stop(context.Context) error {
	if t.process == nil {
		return fmt.Errorf("QEMU target has not yet been started")
	}
	err := t.process.Kill()
	t.process = nil
	return err
}

// Wait waits for the QEMU target to stop.
func (t *QEMUTarget) Wait(ctx context.Context) error {
	return <-t.c
}

func copyImagesToDir(dir string, imgs ...*bootserver.Image) error {
	// Copy each in a goroutine for efficiency's sake.
	errs := make(chan error, len(imgs))
	var wg sync.WaitGroup
	wg.Add(len(imgs))
	for _, img := range imgs {
		go func(img *bootserver.Image) {
			if img.Reader != nil {
				if err := copyImageToDir(dir, img); err != nil {
					errs <- err
				}
			}
			wg.Done()
		}(img)
	}
	wg.Wait()
	select {
	case err := <-errs:
		return err
	default:
		return nil
	}
}

func copyImageToDir(dir string, img *bootserver.Image) error {
	dest := filepath.Join(dir, img.Name)

	f, ok := img.Reader.(*os.File)
	if ok {
		return osmisc.CopyFile(f.Name(), dest)
	}

	f, err := os.Create(dest)
	if err != nil {
		return err
	}
	defer f.Close()

	// Log progress to avoid hitting I/O timeout in case of slow transfers.
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()
	go func() {
		for range ticker.C {
			log.Printf("transferring %s...\n", img.Name)
		}
	}()

	if _, err := io.Copy(f, iomisc.ReaderAtToReader(img.Reader)); err != nil {
		return fmt.Errorf("failed to copy image %q to %q: %v", img.Name, dest, err)
	}
	return nil
}

func overwriteFileWithCopy(path string) error {
	tmpfile, err := ioutil.TempFile(filepath.Dir(path), "botanist")
	if err != nil {
		return err
	}
	defer tmpfile.Close()
	if err := osmisc.CopyFile(path, tmpfile.Name()); err != nil {
		return err
	}
	return os.Rename(tmpfile.Name(), path)
}
