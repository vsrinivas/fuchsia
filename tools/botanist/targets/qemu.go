// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package targets

import (
	"bytes"
	"context"
	"encoding/hex"
	"fmt"
	"io"
	"math/rand"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"

	"go.fuchsia.dev/fuchsia/tools/bootserver"
	"go.fuchsia.dev/fuchsia/tools/botanist"
	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil"
	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/qemu"

	"github.com/creack/pty"
	"golang.org/x/crypto/ssh"
)

const (
	// qemuSystemPrefix is the prefix of the QEMU binary name, which is of the
	// form qemu-system-<QEMU arch suffix>.
	qemuSystemPrefix = "qemu-system"

	// DefaultInterfaceName is the name given to the emulated tap interface.
	defaultInterfaceName = "qemu"

	// DefaultQEMUNodename is the default nodename given to a QEMU target.
	DefaultQEMUNodename = "botanist-target-qemu"

	// The size in bytes of minimimum desired size for the storage-full image.
	// The image should be large enough to hold all downloaded test packages
	// for a given test shard.
	//
	// No host-side disk blocks are allocated on extension (by use of the `fvm`
	// host tool), so the operation is cheap regardless of the size we extend to.
	storageFullMinSize int64 = 10000000000 // 10Gb

	// Minimum number of bytes of entropy bits required for the kernel's PRNG.
	minEntropyBytes uint = 32 // 256 bits
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

	// Logfile saves emulator standard output to a file if set.
	Logfile string `json:"logfile"`

	// Whether User networking is enabled; if false, a Tap interface will be used.
	UserNetworking bool `json:"user_networking"`

	// MinFS is the filesystem to mount as a device.
	MinFS *MinFS `json:"minfs,omitempty"`

	// Path to the fvm host tool.
	FVMTool string `json:"fvm_tool"`

	// Path to the zbi host tool.
	ZBITool string `json:"zbi_tool"`
}

// QEMUTarget is a QEMU target.
type QEMUTarget struct {
	*target
	binary  string
	builder EMUCommandBuilder
	config  QEMUConfig
	opts    Options
	c       chan error
	process *os.Process
	mac     [6]byte
	serial  io.ReadWriteCloser
	ptm     *os.File
	// isQEMU distinguishes a QEMUTarget from an AEMUTarget
	// since AEMUTarget inherits from QEMUTarget.
	// TODO(ihuh): Refactor this to be a general EMUTarget so that
	// a QEMUTarget would always have isQEMU=true, as its name implies.
	isQEMU bool
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
	AddSerial(qemu.Chardev)
	AddNetwork(qemu.Netdev)
	AddKernelArg(string)
	Build() ([]string, error)
	BuildConfig() (qemu.Config, error)
}

// NewQEMUTarget returns a new QEMU target with a given configuration.
func NewQEMUTarget(ctx context.Context, config QEMUConfig, opts Options) (*QEMUTarget, error) {
	qemuTarget, ok := qemuTargetMapping[config.Target]
	if !ok {
		return nil, fmt.Errorf("invalid target %q", config.Target)
	}

	t := &QEMUTarget{
		binary:  fmt.Sprintf("%s-%s", qemuSystemPrefix, qemuTarget),
		builder: &qemu.QEMUCommandBuilder{},
		config:  config,
		opts:    opts,
		c:       make(chan error),
		isQEMU:  true,
	}
	r := rand.New(rand.NewSource(time.Now().UnixNano()))
	if _, err := r.Read(t.mac[:]); err != nil {
		return nil, fmt.Errorf("failed to generate random MAC: %w", err)
	}

	if config.Serial {
		// We can run QEMU 'in a terminal' by creating a pseudoterminal slave and
		// attaching it as the process' std(in|out|err) streams. Running it in a
		// terminal - and redirecting serial to stdio - allows us to use the
		// associated pseudoterminal master as the 'serial device' for the
		// instance.
		var err error
		// TODO(joshuaseaton): Figure out how to manage ownership so that this may
		// be closed.
		t.ptm, t.serial, err = pty.Open()
		if err != nil {
			return nil, fmt.Errorf("failed to create ptm/pts pair: %w", err)
		}
	}
	base, err := newTarget(ctx, DefaultQEMUNodename, "", []string{opts.SSHKey}, t.serial)
	if err != nil {
		return nil, err
	}
	t.target = base
	return t, nil
}

// Nodename returns the name of the target node.
func (t *QEMUTarget) Nodename() string {
	return DefaultQEMUNodename
}

// Serial returns the serial device associated with the target for serial i/o.
func (t *QEMUTarget) Serial() io.ReadWriteCloser {
	return t.serial
}

// SSHKey returns the private SSH key path associated with a previously embedded authorized key.
func (t *QEMUTarget) SSHKey() string {
	return t.opts.SSHKey
}

// SSHClient creates and returns an SSH client connected to the QEMU target.
func (t *QEMUTarget) SSHClient() (*sshutil.Client, error) {
	addr, err := t.IPv6()
	if err != nil {
		return nil, err
	}
	return t.sshClient(addr)
}

// Start starts the QEMU target.
func (t *QEMUTarget) Start(ctx context.Context, images []bootserver.Image, args []string) (err error) {
	// TODO(fxbug.dev/91352): Remove experimental condition once stable.
	useFFX := t.UseFFXExperimental(2)

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
	zbiImageName := "zbi_zircon-a"
	qemuKernelName := "kernel_qemu-kernel"
	qemuKernelLabel := ""
	if t.imageOverrides != nil {
		zbiImageName = fmt.Sprintf("zbi_%s", t.imageOverrides[build.ZbiImage].Name)
		qemuKernelOverride := t.imageOverrides[build.QemuKernel]
		if qemuKernelOverride.Name != "" {
			qemuKernelName = fmt.Sprintf("kernel_%s", qemuKernelOverride.Name)
		} else {
			qemuKernelName = ""
			qemuKernelLabel = qemuKernelOverride.Label
		}
	}
	for _, img := range images {
		switch img.Name {
		case qemuKernelName:
			qemuKernel = img
		case zbiImageName:
			zirconA = img
		case "blk_storage-full":
			storageFull = img
		}
		if qemuKernelLabel != "" && img.Label == qemuKernelLabel {
			qemuKernel = img
		}
	}
	if qemuKernel.Reader == nil {
		return fmt.Errorf("could not find kernel_qemu-kernel")
	}
	if zirconA.Reader == nil {
		return fmt.Errorf("could not find %s", zbiImageName)
	}

	// The QEMU command needs to be invoked within an empty directory, as QEMU
	// will attempt to pick up files from its working directory, one notable
	// culprit being multiboot.bin. This can result in strange behavior.
	workdir, err := os.MkdirTemp("", "qemu-working-dir")
	if err != nil {
		return err
	}
	defer func() {
		if err != nil {
			os.RemoveAll(workdir)
		}
	}()

	if err := copyImagesToDir(ctx, workdir, false, &qemuKernel, &zirconA, &storageFull); err != nil {
		return err
	}

	if t.config.ZBITool != "" && t.SSHKey() != "" {
		signers, err := parseOutSigners([]string{t.SSHKey()})
		if err != nil {
			return fmt.Errorf("could not parse out signers from private keys: %w", err)
		}
		var authorizedKeys []byte
		for _, s := range signers {
			authorizedKey := ssh.MarshalAuthorizedKey(s.PublicKey())
			authorizedKeys = append(authorizedKeys, authorizedKey...)
		}
		if len(authorizedKeys) == 0 {
			return fmt.Errorf("missing authorized keys")
		}
		tmpFile := filepath.Join(workdir, "authorized_keys")
		if err := os.WriteFile(tmpFile, authorizedKeys, os.ModePerm); err != nil {
			return fmt.Errorf("could not write authorized keys to file: %w", err)
		}
		if useFFX {
			t.ffx.ConfigSet(ctx, "ssh.pub", tmpFile)
		}
		if err := embedZBIWithKey(ctx, &zirconA, t.config.ZBITool, tmpFile); err != nil {
			return fmt.Errorf("failed to embed zbi with key: %w", err)
		}
	}

	// Now that the images hav successfully been copied to the working
	// directory, Path points to their path on disk.
	qemuCmd.SetKernel(qemuKernel.Path)
	qemuCmd.SetInitrd(zirconA.Path)

	if storageFull.Path != "" {
		if t.config.FVMTool != "" {
			if err := extendStorageFull(ctx, &storageFull, t.config.FVMTool, storageFullMinSize); err != nil {
				return fmt.Errorf("%s to %d bytes: %w", constants.FailedToExtendFVMMsg, storageFullMinSize, err)
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
			return fmt.Errorf("could not find minfs image %q: %w", t.config.MinFS.Image, err)
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
		ID:     "net0",
		Device: qemu.Device{Model: qemu.DeviceModelVirtioNetPCI},
	}
	netdev.Device.AddOption("mac", net.HardwareAddr(t.mac[:]).String())
	netdev.Device.AddOption("vectors", "8")
	if t.config.UserNetworking {
		netdev.User = &qemu.NetdevUser{}
	} else {
		netdev.Tap = &qemu.NetdevTap{Name: defaultInterfaceName}
	}

	qemuCmd.AddNetwork(netdev)

	chardev := qemu.Chardev{
		ID:     "char0",
		Signal: false,
	}
	if t.config.Logfile != "" {
		logfile, err := filepath.Abs(t.config.Logfile)
		if err != nil {
			return fmt.Errorf("cannot get absolute path for %q: %w", t.config.Logfile, err)
		}
		if err := os.MkdirAll(filepath.Dir(logfile), os.ModePerm); err != nil {
			return fmt.Errorf("failed to make parent dirs of %q: %w", logfile, err)
		}
		chardev.Logfile = logfile
	}
	qemuCmd.AddSerial(chardev)

	// Manually set nodename, since MAC is randomly generated.
	qemuCmd.AddKernelArg("zircon.nodename=" + DefaultQEMUNodename)
	// Disable the virtcon.
	qemuCmd.AddKernelArg("virtcon.disable=true")
	// The system will halt on a kernel panic instead of rebooting.
	qemuCmd.AddKernelArg("kernel.halt-on-panic=true")
	// Print a message if `dm poweroff` times out.
	qemuCmd.AddKernelArg("devmgr.suspend-timeout-debug=true")
	// Disable kernel lockup detector in emulated environments to prevent false alarms from
	// potentially oversubscribed hosts.
	qemuCmd.AddKernelArg("kernel.lockup-detector.critical-section-threshold-ms=0")
	qemuCmd.AddKernelArg("kernel.lockup-detector.critical-section-fatal-threshold-ms=0")
	qemuCmd.AddKernelArg("kernel.lockup-detector.heartbeat-period-ms=0")
	qemuCmd.AddKernelArg("kernel.lockup-detector.heartbeat-age-threshold-ms=0")
	qemuCmd.AddKernelArg("kernel.lockup-detector.heartbeat-age-fatal-threshold-ms=0")

	// Add entropy to simulate bootloader entropy.
	entropy := make([]byte, minEntropyBytes)
	if _, err := rand.Read(entropy); err == nil {
		qemuCmd.AddKernelArg("kernel.entropy-mixin=" + hex.EncodeToString(entropy))
	}
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
	qemuCmd.SetFlag("-monitor", "none")

	if useFFX {
		config, err := qemuCmd.BuildConfig()
		if err != nil {
			return err
		}
		configFile := filepath.Join(workdir, "ffx_emu_config.json")
		if err := jsonutil.WriteToFile(configFile, config); err != nil {
			return err
		}
		// We currently don't have an easy way to get a modular SDK or modify the
		// virtual device properties.
		// TODO(fxbug.dev/95938): Stop rewriting files once there is a better alternative.
		cwd, err := os.Getwd()
		if err != nil {
			return err
		}
		sdkManifestPath := filepath.Join(cwd, ffxutil.SDKManifestPath)
		if err := rewriteSDKManifest(sdkManifestPath, t.config.Target, t.isQEMU); err != nil {
			return err
		}
		if err := rewriteVirtualDevice(filepath.Join(cwd, ffxutil.VirtualDevicePath), t.config, int(storageFullMinSize/1000000000)); err != nil {
			return err
		}
		tools := ffxutil.EmuTools{
			Emulator: absQEMUSystemPath,
			FVM:      t.config.FVMTool,
			ZBI:      t.config.ZBITool,
		}
		err = t.ffx.EmuStart(ctx, cwd, DefaultQEMUNodename, t.isQEMU, configFile, tools)
		if err != nil {
			// The `ffx emu` command may fail but still stage images in its instance dir
			// that need to be cleaned up with a call to `ffx emu stop`.
			if stopErr := t.ffx.EmuStop(ctx); stopErr != nil {
				logger.Errorf(ctx, "failed to stop emulator: %s", stopErr)
			}
		}
		// Show the emulator details for debugging purposes since they are hidden
		// in the config files passed to `ffx emu`.
		if showErr := t.ffx.Run(ctx, "emu", "show"); showErr != nil {
			logger.Errorf(ctx, "failed to run `ffx emu show`: %s", showErr)
		}
		return err
	}

	invocation, err := qemuCmd.Build()
	if err != nil {
		return err
	}

	// TODO(fxbug.dev/43188): We temporarily capture the tail of all stdout and
	// stderr to search for a particular error signature.
	var outputSink bytes.Buffer
	cmd := exec.Command(invocation[0], invocation[1:]...)
	cmd.Dir = workdir
	stdout, stderr, flush := botanist.NewStdioWriters(ctx)
	if t.ptm != nil {
		cmd.Stdin = t.ptm
		cmd.Stdout = io.MultiWriter(t.ptm, &outputSink, stdout)
		cmd.Stderr = io.MultiWriter(t.ptm, &outputSink, stderr)
		cmd.SysProcAttr = &syscall.SysProcAttr{
			Setctty: true,
			Setsid:  true,
		}
	} else {
		cmd.Stdout = io.MultiWriter(&outputSink, stdout)
		cmd.Stderr = io.MultiWriter(&outputSink, stderr)
	}
	logger.Debugf(ctx, "QEMU invocation:\n%s", strings.Join(invocation, " "))

	if err := cmd.Start(); err != nil {
		flush()
		return fmt.Errorf("failed to start: %w", err)
	}
	t.process = cmd.Process

	go func() {
		err := cmd.Wait()
		flush()
		if err != nil {
			err = fmt.Errorf("%s: %w", constants.QEMUInvocationErrorMsg, err)
		}
		t.c <- err
		os.RemoveAll(workdir)
	}()
	return nil
}

// rewriteSDKManifest rewrites the SDK manifest needed by `ffx emu` to only include the tools
// we need from the SDK. That way we don't need to ensure all other files referenced by the
// manifest exist, only the ones included in the manifest.
func rewriteSDKManifest(manifestPath, targetCPU string, isQEMU bool) error {
	// TODO(fxbug.dev/99321): Use the tools from the SDK once they are available for
	// arm64. Until then, we'll have to provide our own.
	manifest, err := ffxutil.GetFFXEmuManifest(manifestPath, targetCPU, []string{})
	if err != nil {
		return err
	}

	if err := jsonutil.WriteToFile(manifestPath, manifest); err != nil {
		return fmt.Errorf("failed to modify sdk manifest: %w", err)
	}
	return nil
}

// rewriteVirtualDevice rewrites the virtual_device config used by `ffx emu` with the
// provided memory and storage values. This is necessary because there is no
// other way to modify these values currently.
func rewriteVirtualDevice(path string, config QEMUConfig, storage int) error {
	device, err := ffxutil.GetVirtualDevice(path)
	if err != nil {
		return err
	}
	device.Data.Hardware.Memory.Quantity = config.Memory
	device.Data.Hardware.Memory.Units = "megabytes"
	device.Data.Hardware.Storage.Quantity = storage
	device.Data.Hardware.Storage.Units = "gigabytes"

	if err := jsonutil.WriteToFile(path, device); err != nil {
		return fmt.Errorf("failed to modify virtual_device: %w", err)
	}
	return nil
}

// Stop stops the QEMU target.
func (t *QEMUTarget) Stop() error {
	// TODO(fxbug.dev/91352): Remove experimental condition once stable.
	if t.UseFFXExperimental(2) {
		return t.ffx.EmuStop(context.Background())
	}
	if t.process == nil {
		return fmt.Errorf("QEMU target has not yet been started")
	}
	logger.Debugf(t.targetCtx, "Sending SIGKILL to %d", t.process.Pid)
	err := t.process.Kill()
	t.process = nil
	t.target.Stop()
	return err
}

// Wait waits for the QEMU target to stop.
func (t *QEMUTarget) Wait(ctx context.Context) error {
	select {
	case err := <-t.c:
		return err
	case <-ctx.Done():
		return ctx.Err()
	}
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
	tmpfile, err := os.CreateTemp(filepath.Dir(path), "botanist")
	if err != nil {
		return err
	}
	defer tmpfile.Close()
	if err := osmisc.CopyFile(path, tmpfile.Name()); err != nil {
		return err
	}
	return os.Rename(tmpfile.Name(), path)
}

func embedZBIWithKey(ctx context.Context, zbiImage *bootserver.Image, zbiTool string, authorizedKeysFile string) error {
	absToolPath, err := filepath.Abs(zbiTool)
	if err != nil {
		return err
	}
	logger.Debugf(ctx, "embedding %s with key %s", zbiImage.Name, authorizedKeysFile)
	cmd := exec.CommandContext(ctx, absToolPath, "-o", zbiImage.Path, zbiImage.Path, "--entry", fmt.Sprintf("data/ssh/authorized_keys=%s", authorizedKeysFile))
	stdout, stderr, flush := botanist.NewStdioWriters(ctx)
	defer flush()
	cmd.Stdout = stdout
	cmd.Stderr = stderr
	if err := cmd.Run(); err != nil {
		return err
	}
	return nil
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
	stdout, stderr, flush := botanist.NewStdioWriters(ctx)
	defer flush()
	cmd.Stdout = stdout
	cmd.Stderr = stderr
	if err := cmd.Run(); err != nil {
		return err
	}
	storageFull.Size = size
	return nil
}
