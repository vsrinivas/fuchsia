// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package target

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/creack/pty"

	"go.fuchsia.dev/fuchsia/tools/bootserver"
	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/qemu"
)

const (
	// qemuSystemPrefix is the prefix of the QEMU binary name, which is of the
	// form qemu-system-<QEMU arch suffix>.
	qemuSystemPrefix = "qemu-system"

	// DefaultInterfaceName is the name given to the emulated tap interface.
	defaultInterfaceName = "qemu"

	// Default networking values.
	defaultMACAddr       = "52:54:00:63:5e:7a"
	defaultLinkLocalAddr = "fe80::5054:ff:fe63:5e7a"

	// DefaultQEMUNodename is the default nodename given to a target with the
	// default QEMU MAC address.
	DefaultQEMUNodename = "step-atom-yard-juicy"

	// The size in bytes of minimimum desired size for the storage-full image.
	// The image should be large enough to hold all downloaded test packages
	// for a given test shard.
	//
	// No host-side disk blocks are allocated on extension (by use of the `fvm`
	// host tool), so the operation is cheap regardless of the size we extend to.
	storageFullMinSize int64 = 10000000000 // 10Gb
)

// qemuTargetMapping maps the Fuchsia target name to the name recognized by QEMU.
var qemuTargetMapping = map[string]qemu.Target{
	"x64":   qemu.TargetEnum.X86_64,
	"arm64": qemu.TargetEnum.AArch64,
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

	// Serial gives whether to create a 'serial device' for the QEMU instance.
	// This option should be used judiciously, as it can slow the process down.
	Serial bool `json:"serial"`

	// Whether User networking is enabled; if false, a Tap interface will be used.
	UserNetworking bool `json:"user_networking"`

	// MinFS is the filesystem to mount as a device.
	MinFS *MinFS `json:"minfs,omitempty"`

	// Path to the fvm host tool.
	FVMTool string `json:"fvm_tool"`
}

// QEMUTarget is a QEMU target.
type QEMUTarget struct {
	binary  string
	builder EMUCommandBuilder
	config  QEMUConfig
	opts    Options
	c       chan error
	process *os.Process
	serial  io.ReadWriteCloser
	ptm     *os.File
}

// EMUCommandBuilder defines the common set of functions used to build up an
// EMU command-line.
type EMUCommandBuilder interface {
	SetFlag(...string)
	SetBinary(string)
	SetKernel(string)
	SetInitrd(string)
	SetTarget(qemu.Target, bool)
	SetMemory(int)
	SetCPUCount(int)
	AddVirtioBlkPciDrive(qemu.Drive)
	AddNetwork(qemu.Netdev)
	AddKernelArg(string)
	Build() ([]string, error)
}

// NewQEMUTarget returns a new QEMU target with a given configuration.
func NewQEMUTarget(config QEMUConfig, opts Options) (*QEMUTarget, error) {
	var serial io.ReadWriteCloser
	var ptm *os.File
	if config.Serial {
		// We can run QEMU 'in a terminal' by creating a pseudoterminal slave and
		// attaching it as the process' std(in|out|err) streams. Running it in a
		// terminal - and redirecting serial to stdio - allows us to use the
		// associated pseudoterminal master as the 'serial device' for the
		// instance.
		var pts *os.File
		var err error
		ptm, pts, err = pty.Open()
		if err != nil {
			return nil, fmt.Errorf("failed to create ptm/pts pair: %w", err)
		}
		serial = pts
	}

	qemuTarget, ok := qemuTargetMapping[config.Target]
	if !ok {
		return nil, fmt.Errorf("invalid target %q", config.Target)
	}

	// TODO(joshuaseaton): Figure out how to manage ownership of pts so that it
	// may be closed.
	return &QEMUTarget{
		binary:  fmt.Sprintf("%s-%s", qemuSystemPrefix, qemuTarget),
		builder: &qemu.QEMUCommandBuilder{},
		config:  config,
		opts:    opts,
		c:       make(chan error),
		serial:  serial,
		ptm:     ptm,
	}, nil
}

// Nodename returns the name of the target node.
func (t *QEMUTarget) Nodename() string {
	return DefaultQEMUNodename
}

// IPv6Addr returns the global unicast IPv6 address of the qemu instance.
func (t *QEMUTarget) IPv6Addr() string {
	return fmt.Sprintf("%s%%%s", defaultLinkLocalAddr, defaultInterfaceName)
}

// IPv4Addr returns a nil address, as DHCP is not currently configured.
func (t *QEMUTarget) IPv4Addr() (net.IP, error) {
	return nil, nil
}

// Serial returns the serial device associated with the target for serial i/o.
func (t *QEMUTarget) Serial() io.ReadWriteCloser {
	return t.serial
}

// SSHKey returns the private SSH key path associated with a previously embedded authorized key.
func (t *QEMUTarget) SSHKey() string {
	return t.opts.SSHKey
}

// Start starts the QEMU target.
func (t *QEMUTarget) Start(ctx context.Context, images []bootserver.Image, args []string) (err error) {
	// We create a working directory for the QEMU process below, but cannot
	// clean it up until we error or until the process finishes. The former is
	// handled in this block, while the latter is handled in a goroutine below.
	var workdir string
	go func() {
		if workdir != "" && err != nil {
			os.RemoveAll(workdir)
		}
	}()

	if t.process != nil {
		return fmt.Errorf("a process has already been started with PID %d", t.process.Pid)
	}
	qemuCmd := t.builder

	qemuTarget, ok := qemuTargetMapping[t.config.Target]
	if !ok {
		return fmt.Errorf("invalid target %q", t.config.Target)
	}
	qemuCmd.SetTarget(qemuTarget, t.config.KVM)

	if t.config.Path == "" {
		return fmt.Errorf("directory must be set")
	}
	qemuSystem := filepath.Join(t.config.Path, t.binary)
	absQEMUSystemPath, err := normalizeFile(qemuSystem)
	if err != nil {
		return fmt.Errorf("could not find qemu binary %q: %w", qemuSystem, err)
	}
	qemuCmd.SetBinary(absQEMUSystemPath)

	var qemuKernel, zirconA, storageFull bootserver.Image
	for _, img := range images {
		switch img.Name {
		case "kernel_qemu-kernel":
			qemuKernel = img
		case "zbi_zircon-a":
			zirconA = img
		case "blk_storage-full":
			storageFull = img
		}
	}
	if qemuKernel.Reader == nil {
		return fmt.Errorf("could not find kernel_qemu-kernel")
	}
	if zirconA.Reader == nil {
		return fmt.Errorf("could not find zbi_zircon-a")
	}
	// The QEMU command needs to be invoked within an emptm directory, as QEMU
	// will attempt to pick up files from its working directory, one notable
	// culprit being multiboot.bin.  This can result in strange behavior.
	workdir, err = ioutil.TempDir("", "qemu-working-dir")
	if err != nil {
		return err
	}

	if err := copyImagesToDir(ctx, workdir, &qemuKernel, &zirconA, &storageFull); err != nil {
		return err
	}

	// Now that the images hav successfully been copied to the working
	// directory, Path points to their path on disk.
	qemuCmd.SetKernel(qemuKernel.Path)
	qemuCmd.SetInitrd(zirconA.Path)

	if storageFull.Path != "" {
		if t.config.FVMTool != "" {
			if err := extendStorageFull(ctx, &storageFull, t.config.FVMTool, storageFullMinSize); err != nil {
				return fmt.Errorf("failed to extend fvm.blk to %d bytes: %v", storageFullMinSize, err)
			}
		}
		qemuCmd.AddVirtioBlkPciDrive(qemu.Drive{
			ID:   "maindisk",
			File: storageFull.Path,
		})
	}

	if t.config.MinFS != nil {
		absMinFsPath, err := normalizeFile(t.config.MinFS.Image)
		if err != nil {
			return fmt.Errorf("could not find minfs image %q: %v", t.config.MinFS.Image, err)
		}
		// Swarming hard-links Isolate downloads with a cache and the very same
		// cached minfs image will be used across multiple tasks. To ensure
		// that it remains blank, we must break its link.
		if err := overwriteFileWithCopy(absMinFsPath); err != nil {
			return err
		}
		qemuCmd.AddVirtioBlkPciDrive(qemu.Drive{
			ID:   "testdisk",
			File: absMinFsPath,
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
	qemuCmd.AddNetwork(netdev)

	// Disable the virtcon.
	qemuCmd.AddKernelArg("virtcon.disable=true")
	// The system will halt on a kernel panic instead of rebooting.
	qemuCmd.AddKernelArg("kernel.halt-on-panic=true")
	// Print a message if `dm poweroff` times out.
	qemuCmd.AddKernelArg("devmgr.suspend-timeout-debug=true")
	// Do not print colors.
	qemuCmd.AddKernelArg("TERM=dumb")
	if t.config.Target == "x64" {
		// Necessary to redirect to stdout.
		qemuCmd.AddKernelArg("kernel.serial=legacy")
	}
	for _, arg := range args {
		qemuCmd.AddKernelArg(arg)
	}

	qemuCmd.SetCPUCount(t.config.CPU)
	qemuCmd.SetMemory(t.config.Memory)
	qemuCmd.SetFlag("-nographic")
	qemuCmd.SetFlag("-serial", "stdio")
	qemuCmd.SetFlag("-monitor", "none")

	invocation, err := qemuCmd.Build()
	if err != nil {
		return err
	}

	// TODO(fxbug.dev/43188): We temporarily capture the tail of all stdout and
	// stderr to search for a particular error signature.
	var outputSink bytes.Buffer
	cmd := exec.Command(invocation[0], invocation[1:]...)
	cmd.Dir = workdir
	if t.ptm != nil {
		cmd.Stdin = t.ptm
		cmd.Stdout = io.MultiWriter(t.ptm, &outputSink, os.Stdout)
		cmd.Stderr = io.MultiWriter(t.ptm, &outputSink, os.Stderr)
		cmd.SysProcAttr = &syscall.SysProcAttr{
			Setctty: true,
			Setsid:  true,
			Ctty:    int(t.ptm.Fd()),
		}
	} else {
		cmd.Stdout = io.MultiWriter(&outputSink, os.Stdout)
		cmd.Stderr = io.MultiWriter(&outputSink, os.Stderr)
	}
	logger.Debugf(ctx, "QEMU invocation:\n%s", strings.Join(invocation, " "))

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to start: %w", err)
	}
	t.process = cmd.Process

	go func() {
		err := cmd.Wait()
		if err != nil {
			checkForEBUSY(ctx, &outputSink)
			err = fmt.Errorf("%s: %w", constants.QEMUInvocationErrorMsg, err)
		}
		t.c <- err
		os.RemoveAll(workdir)
	}()
	return nil
}

// checkForEBUSY runs an lsof on /dev/net/tun if QEMU startup failed due to an EBUSY.
func checkForEBUSY(ctx context.Context, buffer *bytes.Buffer) {
	content, err := buffer.ReadString('\n')
	if err != nil {
		return
	}
	if strings.Contains(strings.ToLower(content), syscall.EBUSY.Error()) {
		logger.Debugf(ctx, "fxbug.dev/43188: QEMU startup failed with an EBUSY, contact rudymathu@ for triage")

		// This command prints out all processes using /dev/net/tun.
		cmd := exec.Command("lsof", "+c", "0", "/dev/net/tun")
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		if err := cmd.Run(); err != nil {
			logger.Errorf(ctx, "running lsof failed: %v", err)
		}
	}
}

// Stop stops the QEMU target.
func (t *QEMUTarget) Stop(ctx context.Context) error {
	if t.process == nil {
		return fmt.Errorf("QEMU target has not yet been started")
	}
	logger.Debugf(ctx, "Sending SIGKILL to %d", t.process.Pid)
	err := t.process.Kill()
	t.process = nil
	return err
}

// Wait waits for the QEMU target to stop.
func (t *QEMUTarget) Wait(ctx context.Context) error {
	return <-t.c
}

func copyImagesToDir(ctx context.Context, dir string, imgs ...*bootserver.Image) error {
	// Copy each in a goroutine for efficiency's sake.
	errs := make(chan error, len(imgs))
	var wg sync.WaitGroup
	wg.Add(len(imgs))
	for _, img := range imgs {
		go func(img *bootserver.Image) {
			if img.Reader != nil {
				if err := copyImageToDir(ctx, dir, img); err != nil {
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

func copyImageToDir(ctx context.Context, dir string, img *bootserver.Image) error {
	dest := filepath.Join(dir, img.Name)

	f, ok := img.Reader.(*os.File)
	if ok {
		if err := osmisc.CopyFile(f.Name(), dest); err != nil {
			return err
		}
		img.Path = dest
		return nil
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
			logger.Debugf(ctx, "transferring %s...\n", img.Name)
		}
	}()

	if _, err := io.Copy(f, iomisc.ReaderAtToReader(img.Reader)); err != nil {
		return fmt.Errorf("failed to copy image %q to %q: %w", img.Name, dest, err)
	}
	img.Path = dest

	// We no longer need the reader at this point.
	c, ok := img.Reader.(io.Closer)
	if ok {
		c.Close()
	}
	img.Reader = nil
	return nil
}

func normalizeFile(path string) (string, error) {
	if _, err := os.Stat(path); err != nil {
		return "", err
	}
	absPath, err := filepath.Abs(path)
	if err != nil {
		return "", err
	}
	return absPath, nil
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

func extendStorageFull(ctx context.Context, storageFull *bootserver.Image, fvmTool string, size int64) error {
	if storageFull.Size >= size {
		return nil
	}
	absToolPath, err := filepath.Abs(fvmTool)
	if err != nil {
		return err
	}
	logger.Debugf(ctx, "extending fvm.blk to %d bytes", size)
	cmd := exec.CommandContext(ctx, absToolPath, storageFull.Path, "extend", "--length", strconv.Itoa(int(size)))
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return err
	}
	storageFull.Size = size
	return nil
}
