// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package emulator

import (
	"archive/tar"
	"bufio"
	"compress/gzip"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

// Untar untars a tar.gz file into a directory.
func untar(dst string, src string) error {
	f, err := os.Open(src)
	if err != nil {
		return err
	}
	defer f.Close()

	gz, err := gzip.NewReader(f)
	if err != nil {
		return err
	}
	defer gz.Close()
	for tr := tar.NewReader(gz); ; {
		header, err := tr.Next()
		if err == io.EOF {
			return nil
		} else if err != nil {
			return err
		}

		path := filepath.Join(dst, header.Name)
		info := header.FileInfo()
		if info.IsDir() {
			if err := os.MkdirAll(path, info.Mode()); err != nil {
				return err
			}
		} else {
			if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
				return err
			}

			f, err := os.OpenFile(path, os.O_CREATE|os.O_EXCL|os.O_WRONLY, info.Mode())
			if err != nil {
				return err
			}
			_, err = io.Copy(f, tr)
			f.Close()
			if err != nil {
				return err
			}
		}
	}
}

// Distribution is a collection of QEMU-related artifacts.
//
// Delete must be called once done with it.
type Distribution struct {
	testDataDir  string
	unpackedPath string
	Emulator     Emulator
}

// Arch is the architecture to emulate.
type Arch int

const (
	X64 Arch = iota
	Arm64
)

// Emulator is the emulator to use.
type Emulator int

const (
	Qemu Emulator = iota
	Femu
)

// Disk represents a single disk that will be attached to the virtual machine.
type Disk struct {
	Path string
	USB  bool
}

// Params describes how to run an emulator instance.
type Params struct {
	Arch             Arch
	ZBI              string
	AppendCmdline    string
	Networking       bool
	DisableKVM       bool
	DisableDebugExit bool
	Disks            []Disk
}

// Instance is a live emulator instance.
type Instance struct {
	cmd      *exec.Cmd
	piped    *exec.Cmd
	stdin    *bufio.Writer
	stdout   *bufio.Reader
	stderr   *bufio.Reader
	emulator Emulator
}

// DistributionParams is passed to UnpackFrom().
type DistributionParams struct {
	Emulator Emulator
}

// Unpack unpacks the QEMU distribution.
//
// TODO(fxbug.dev/58804): Replace all call sites to UnpackFrom.
func Unpack() (*Distribution, error) {
	ex, err := os.Executable()
	if err != nil {
		return nil, err
	}
	return UnpackFrom(filepath.Join(filepath.Dir(ex), "test_data"), DistributionParams{Emulator: Qemu})
}

// UnpackFrom unpacks the emulator distribution.
//
// path is the path to host_x64/test_data containing the emulator.
// emulator is the emulator to unpack.
func UnpackFrom(path string, distroParams DistributionParams) (*Distribution, error) {
	// Since the emulator will be started from a different directory, make the base path
	// absolute.
	path, err := filepath.Abs(path)
	if err != nil {
		return nil, err
	}
	emulator_file := "qemu.tar.gz"
	if distroParams.Emulator == Femu {
		emulator_file = "femu.tar.gz"
	}
	archivePath := filepath.Join(path, "emulator", emulator_file)
	unpackedPath, err := ioutil.TempDir("", "emulator-distro")
	if err != nil {
		return nil, err
	}
	if err = untar(unpackedPath, archivePath); err != nil {
		os.RemoveAll(unpackedPath)
		return nil, err
	}
	return &Distribution{testDataDir: path, unpackedPath: unpackedPath, Emulator: distroParams.Emulator}, nil
}

// Delete removes the emulator-related artifacts.
func (d *Distribution) Delete() error {
	return os.RemoveAll(d.unpackedPath)
}

func (d *Distribution) systemPath(arch Arch) string {
	if d.Emulator == Femu {
		// FEMU has one binary for all arches.
		return filepath.Join(d.unpackedPath, "emulator")
	}
	switch arch {
	case X64:
		return filepath.Join(d.unpackedPath, "bin", "qemu-system-x86_64")
	case Arm64:
		return filepath.Join(d.unpackedPath, "bin", "qemu-system-aarch64")
	}
	return ""
}

func (d *Distribution) kernelPath(arch Arch) string {
	switch arch {
	case X64:
		return filepath.Join(d.testDataDir, "emulator", "multiboot.bin")
	case Arm64:
		return filepath.Join(d.testDataDir, "emulator", "qemu-boot-shim.bin")
	}
	return ""
}

// Check to see whether the current host supports KVM.
// Currently only suports X64 Linux.
func hostSupportsKVM(arch Arch) bool {
	if runtime.GOOS != "linux" {
		return false
	}
	if arch != X64 || runtime.GOARCH != "amd64" {
		return false
	}
	_, err := os.OpenFile("/dev/kvm", os.O_RDONLY, 0)
	if err != nil {
		return false
	}
	return true
}

// TargetCPU returs the target CPU used by the build that produced this library.
func (d *Distribution) TargetCPU() (Arch, error) {
	path := filepath.Join(d.testDataDir, "emulator", "target_cpu.txt")
	bytes, err := ioutil.ReadFile(path)
	if err != nil {
		return X64, err
	}
	name := string(bytes)
	switch name {
	case "x64":
		return X64, nil
	case "arm64":
		return Arm64, nil
	}
	return X64, fmt.Errorf("unknown target CPU: %s", name)
}

func (d *Distribution) appendCommonQemuArgs(params Params, args []string) []string {
	// Append architecture specific QEMU options.  These options
	// are meant to mirror those used by `fx qemu`.
	if params.Arch == Arm64 {
		args = append(args, "-machine", "virtualization=true",
			"-cpu", "max", "-machine", "virt-2.12,gic-version=3")
	} else if params.Arch == X64 {
		args = append(args, "-machine", "q35", "-cpu", "Haswell,+smap,-check,-fsgsbase")
		if !params.DisableKVM && hostSupportsKVM(params.Arch) {
			args = append(args, "-enable-kvm")
		}
		if !params.DisableDebugExit {
			args = append(args, "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04")
		}
	} else {
		panic("unsupported architecture")
	}

	if params.Networking {
		args = append(args, "-nic", "tap,ifname=qemu,script=no,downscript=no,model=virtio-net-pci")
	} else {
		args = append(args, "-nic", "none")
	}

	return args
}

func (d *Distribution) appendCommonFemuArgs(params Params, args []string) []string {
	// These options should mirror what's used by `fx emu`.

	if params.Arch == Arm64 {
		args = append(args, "-avd-arch arm64")
	}

	args = append(args, "-feature", "VirtioInput,GLDirectMem,KVM,Vulkan")
	args = append(args, "-gpu", "auto")
	args = append(args, "-no-window")

	// Everything after `-fuchsia` is passed directly to qemu by femu.
	args = append(args, "-fuchsia")
	// `fx emu` has slightly different semantics to `fx qemu` for architecture-specific stuff.
	if params.Arch == Arm64 {
		args = append(args, "-machine", "virt")
	} else if params.Arch == X64 {
		args = append(args, "-machine", "q35")
		if !params.DisableKVM && hostSupportsKVM(params.Arch) {
			args = append(args, "-enable-kvm")
			args = append(args, "-cpu", "host,migratable=no,+invtsc")
		} else {
			args = append(args, "-cpu", "Haswell,+smap,-check,-fsgsbase")
		}
		if !params.DisableDebugExit {
			args = append(args, "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04")
		}
	} else {
		panic("unsupported architecture")
	}
	args = append(args, "-vga", "none")

	if params.Networking {
		args = append(args, "-netdev", "type=tap,ifname=qemu,id=net0,script=no")
		args = append(args, "-device", "virtio-net-pci,vectors=8,netdev=net0,mac=52:54:00:63:5e:7a")
	} else {
		args = append(args, "-net", "none")
	}

	return args
}

func (d *Distribution) appendCommonArgs(params Params, args []string) []string {
	args = append(args, "-kernel", d.kernelPath(params.Arch))
	if d.Emulator == Qemu {
		args = d.appendCommonQemuArgs(params, args)
	} else if d.Emulator == Femu {
		args = d.appendCommonFemuArgs(params, args)
	}

	diskParams := []string{}
	hasUsbDisk := false

	for i, disk := range params.Disks {
		drive_id := fmt.Sprintf("disk%02d", i)
		diskParams = append(diskParams, "-drive", "if=none,id="+drive_id+",file="+disk.Path+",format=raw")
		if disk.USB {
			hasUsbDisk = true
			diskParams = append(diskParams, "-device", "usb-storage,drive="+drive_id)
		} else {
			diskParams = append(diskParams, "-device", "virtio-blk-pci,drive="+drive_id)
		}
	}

	if hasUsbDisk {
		// If we have USB disks, we also need to emulate a USB host controller.
		args = append(args, "-device", "qemu-xhci,id=xhci")
	}

	args = append(args, diskParams...)

	// Ask QEMU to emit a message on stderr once the VM is running
	// so we'll know whether QEMU has started or not.
	args = append(args, "-trace", "enable=vm_state_notify")
	args = append(args, "-smp", "4,threads=2")

	args = append(args, "-m", "8192")
	args = append(args, "-nographic")

	return args
}

func getCommonKernelCmdline(params Params) string {
	cmdline := "kernel.serial=legacy " +
		"kernel.entropy-mixin=1420bb81dc0396b37cc2d0aa31bb2785dadaf9473d0780ecee1751afb5867564 " +
		"kernel.halt-on-panic=true " +
		// Disable lockup detector heartbeats. In emulated environments, virtualized CPUs
		// may be starved or fail to execute in a timely fashion, resulting in apparent
		// lockups. See fxbug.dev/65990.
		"kernel.lockup-detector.heartbeat-period-ms=0 " +
		"kernel.lockup-detector.heartbeat-age-threshold-ms=0"
	if params.AppendCmdline != "" {
		cmdline += " "
		cmdline += params.AppendCmdline
	}
	return cmdline
}

// Create creates an instance of the emulator with the given parameters.
func (d *Distribution) Create(params Params) *Instance {
	if params.ZBI == "" {
		panic("ZBI must be specified")
	}
	args := []string{}
	args = d.appendCommonArgs(params, args)
	args = append(args, "-initrd", params.ZBI)
	args = append(args, "-append", getCommonKernelCmdline(params))
	path := d.systemPath(params.Arch)
	fmt.Printf("Running %s %s\n", path, args)

	i := &Instance{
		cmd:      exec.Command(path, args...),
		emulator: d.Emulator,
	}
	// QEMU looks in the cwd for some specially named files, in particular
	// multiboot.bin, so avoid picking those up accidentally. See
	// https://fxbug.dev/53751.
	// TODO(fxbug.dev/58804): Remove this.
	i.cmd.Dir = "/"

	return i
}

// RunNonInteractive runs an instance of the emulator that runs a single command and
// returns the log that results from doing so.
//
// This mode is non-interactive and is intended specifically to test the case
// where the serial port has been disabled. The following modifications are
// made to the emulator invocation compared with Create()/Start():
//
//  - amalgamate the given ZBI into a larger one that includes an additional
//    entry of a script which includes commands to run.
//  - that script mounts a disk created on the host in /tmp, and runs the
//    given command with output redirected to a file also on the /tmp disk
//  - the script triggers shutdown of the machine
//  - after emulator shutdown, the log file is extracted and returned.
//
// In order to achieve this, here we need to create the host minfs
// file system, write the commands to run, build the augmented .zbi to
// be used to boot. We then use Start() and wait for shutdown.
// Finally, extract and return the log from the minfs disk.
func (d *Distribution) RunNonInteractive(toRun, hostPathMinfsBinary, hostPathZbiBinary string, params Params) (string, string, error) {
	root, err := ioutil.TempDir("", "qemu")
	if err != nil {
		return "", "", err
	}
	log, logerr, err := d.runNonInteractive(root, toRun, hostPathMinfsBinary, hostPathZbiBinary, params)
	if err2 := os.RemoveAll(root); err == nil {
		err = err2
	}
	return log, logerr, err
}

func (d *Distribution) runNonInteractive(root, toRun, hostPathMinfsBinary, hostPathZbiBinary string, params Params) (string, string, error) {
	// Write runcmds that mounts the results disk, runs the requested command, and
	// shuts down.
	b := `mkdir /tmp/testdata-fs
waitfor class=block topo=/dev/sys/pci/00:06.0/virtio-block/block timeout=60000
mount /dev/sys/pci/00:06.0/virtio-block/block /tmp/testdata-fs
` + toRun + ` 2>/tmp/testdata-fs/err.txt >/tmp/testdata-fs/log.txt
umount /tmp/testdata-fs
dm poweroff
`
	runcmds := filepath.Join(root, "runcmds.txt")
	if err := ioutil.WriteFile(runcmds, []byte(b), 0666); err != nil {
		return "", "", err
	}
	// Make a minfs filesystem to mount in the target.
	fs := filepath.Join(root, "a.fs")
	cmd := exec.Command(hostPathMinfsBinary, fs+"@100M", "mkfs")
	if err := cmd.Run(); err != nil {
		return "", "", err
	}

	// Create the new initrd that references the runcmds file.
	zbi := filepath.Join(root, "a.zbi")
	cmd = exec.Command(hostPathZbiBinary, "-o", zbi, params.ZBI, "-e", "runcmds="+runcmds)
	if err := cmd.Run(); err != nil {
		return "", "", err
	}

	// Build up the emulator command line from common arguments and the extra goop to
	// add the temporary disk at 00:06.0. This follows how infra runs qemu with an
	// extra disk via botanist.
	path := d.systemPath(params.Arch)
	args := []string{}
	args = d.appendCommonArgs(params, args)
	args = append(args, "-initrd", zbi)
	args = append(args, "-object", "iothread,id=resultiothread")
	args = append(args, "-drive", "id=resultdisk,file="+fs+",format=raw,if=none,cache=unsafe,aio=threads")
	args = append(args, "-device", "virtio-blk-pci,drive=resultdisk,iothread=resultiothread,addr=6.0")
	cmdline := getCommonKernelCmdline(params)
	cmdline += " zircon.autorun.boot=/boot/bin/sh+/boot/runcmds"
	args = append(args, "-append", cmdline)

	fmt.Printf("Running non-interactive %s %s\n", path, args)
	cmd = exec.Command(path, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	// QEMU looks in the cwd for some specially named files, in particular
	// multiboot.bin, so avoid picking those up accidentally. See
	// https://fxbug.dev/53751.
	// TODO(fxbug.dev/58804): Remove this.
	cmd.Dir = "/"
	if err := cmd.Start(); err != nil {
		return "", "", err
	}
	defer cmd.Process.Kill()
	if err := cmd.Wait(); err != nil {
		return "", "", err
	}

	log := filepath.Join(root, "log.txt")
	logerr := filepath.Join(root, "err.txt")
	cmd = exec.Command(hostPathMinfsBinary, fs, "cp", "::/log.txt", log)
	if err := cmd.Run(); err != nil {
		return "", "", err
	}
	cmd = exec.Command(hostPathMinfsBinary, fs, "cp", "::/err.txt", logerr)
	if err := cmd.Run(); err != nil {
		return "", "", err
	}

	retLog, err := ioutil.ReadFile(log)
	if err != nil {
		return "", "", err
	}
	retErr, err := ioutil.ReadFile(logerr)
	if err != nil {
		return "", "", err
	}
	fmt.Printf("===== %s non-interactive run stdout =====\n%s\n", toRun, retLog)
	fmt.Printf("===== %s non-interactive run stderr =====\n%s\n", toRun, retErr)
	fmt.Printf("===== %s end =====\n", toRun)
	return string(retLog), string(retErr), nil
}

// Start the emulator instance.
func (i *Instance) Start() error {
	return i.StartPiped(nil)
}

// StartPiped starts the emulator instance with stdin/stdout piped through a
// different process.
//
// Assumes that the stderr from the piped process should replace the stdout
// from the emulator.
func (i *Instance) StartPiped(piped *exec.Cmd) error {
	stdin, err := i.cmd.StdinPipe()
	if err != nil {
		return err
	}
	stdout, err := i.cmd.StdoutPipe()
	if err != nil {
		return err
	}
	stderr, err := i.cmd.StderrPipe()
	if err != nil {
		return err
	}

	if piped != nil {
		piped.Stdin = stdout
		piped.Stdout = stdin
		pipedStderr, err := piped.StderrPipe()
		if err != nil {
			return err
		}
		i.stdout = bufio.NewReader(pipedStderr)
		i.stderr = bufio.NewReader(stderr)
		if err = piped.Start(); err != nil {
			return err
		}
		i.piped = piped
	} else {
		i.stdin = bufio.NewWriter(stdin)
		i.stdout = bufio.NewReader(stdout)
		i.stderr = bufio.NewReader(stderr)
	}

	startErr := i.cmd.Start()

	// Look for very early log message to validate that the emulator likely started
	// correctly. Loop for a while to give the emulator a chance to boot.
	fmt.Println("Checking for QEMU boot...")
	for j := 0; j < 100; j++ {

		if i.emulator == Femu {
			// FEMU isn't built with support for outputting trace events.
			// Instead we look for a message that occurs very early during Zircon boot.
			if i.checkForLogMessage(i.stdout, "welcome to Zircon") == nil {
				break
			}
		} else {
			// The flag `-trace enable=vm_state_notify` will cause qemu to
			// print this message early in boot.
			if i.checkForLogMessage(i.stderr, "vm_state_notify running") == nil {
				break
			}
		}

		time.Sleep(100 * time.Millisecond)
	}

	return startErr
}

// Kill terminates the emulator instance.
func (i *Instance) Kill() error {
	var err error
	if i.piped != nil {
		err = i.piped.Process.Kill()
	}
	if err2 := i.cmd.Process.Kill(); err2 != nil {
		return err2
	}
	return err
}

func printWhileWait(r *bufio.Reader, proc *os.Process) (*os.ProcessState, error) {
	stop := make(chan struct{})
	defer close(stop)
	go func() {
		for {
			select {
			case <-stop:
				return
			default:
				if line, err := r.ReadString('\n'); err == nil {
					fmt.Print(line)
				}
			}
		}
	}()

	ps, err := proc.Wait()
	return ps, err
}

// Wait for the emulator instance to terminate
func (i *Instance) Wait() (*os.ProcessState, error) {
	if i.piped != nil {
		if ps, err := printWhileWait(i.stdout, i.piped.Process); err != nil {
			return ps, err
		}
	}
	return printWhileWait(i.stdout, i.cmd.Process)
}

// RunCommand runs the given command in the serial console for the emulator
// instance.
func (i *Instance) RunCommand(cmd string) error {
	_, err := fmt.Fprintf(i.stdin, "%s\n", cmd)
	if err != nil {
		// TODO(maruel): remove once call sites are updated.
		panic(err)
		return err
	}
	err = i.stdin.Flush()
	if err != nil {
		// TODO(maruel): remove once call sites are updated.
		panic(err)
	}
	return err
}

// WaitForLogMessage reads log messages from the emulator instance until it reads a
// message that contains the given string.
//
// panic()s on error (and in particular if the string is not seen until EOF).
func (i *Instance) WaitForLogMessage(msg string) error {
	return i.WaitForLogMessages([]string{msg})
}

// WaitForLogMessages reads log messages from the emulator instance until it reads all
// message in |msgs|. The log messages can appear in *any* order. Only one
// expected message from |msgs| is retired per matching actual message even if
// multiple messages from |msgs| match the log line.
//
// panic()s on error (and in particular if the string is not seen until EOF).
func (i *Instance) WaitForLogMessages(msgs []string) error {
	err := i.checkForLogMessages(i.stdout, msgs)
	if err != nil {
		// TODO(maruel): remove once call sites are updated.
		panic(err)
	}
	return err
}

// WaitForLogMessageAssertNotSeen is the same as WaitForLogMessage() but with
// the addition that it will panic if |notSeen| is contained in a retrieved
// message.
func (i *Instance) WaitForLogMessageAssertNotSeen(msg string, notSeen string) error {
	for {
		line, err := i.stdout.ReadString('\n')
		if err != nil {
			// TODO(maruel): remove once call sites are updated.
			panic(err)
			return err
		}
		if strings.Contains(line, msg) {
			return nil
		}
		if strings.Contains(line, notSeen) {
			// TODO(maruel): remove once call sites are updated.
			panic(notSeen + " was in output")
			return errors.New(notSeen + " was in output")
		}
	}
}

// AssertLogMessageNotSeenWithinTimeout will fail if |notSeen| is seen within the
// |timeout| period. This function will timeout as success if more than |timeout| has
// passed without seeing |notSeen|.
func (i *Instance) AssertLogMessageNotSeenWithinTimeout(notSeen string, timeout time.Duration) error {
	// ReadString is blocking, we need to make sure it respects the global timeout.
	seen := make(chan struct{})
	stop := make(chan struct{})
	defer close(stop)
	go func() {
		defer close(seen)
		for {
			select {
			case <-stop:
				return
			default:
				if line, err := i.stdout.ReadString('\n'); err == nil {
					if strings.Contains(line, notSeen) {
						seen <- struct{}{}
						return
					}
				}
			}
		}
	}()
	select {
	case <-seen:
		panic(notSeen + " was in output")
		return errors.New(notSeen + " was in output")
	case <-time.After(timeout):
		return nil
	}
}

// Reset display: ESC c
// Reset screen mode: ESC [ ? 7 l
// Move cursor home: ESC [ 2 J
// All text attributes off: ESC [ 0 m
const emuClearPrefix = "\x1b\x63\x1b\x5b\x3f\x37\x6c\x1b\x5b\x32\x4a\x1b\x5b\x30\x6d"

// Reads all messages from |b| and tests if |msg| appears. Returns error if any.
func (i *Instance) checkForLogMessage(b *bufio.Reader, msg string) error {
	return i.checkForLogMessages(b, []string{msg})
}

// Reads all messages from |b| and tests if all messages of |msgs| appear in *any* order. Returns
// error if any.
func (i *Instance) checkForLogMessages(b *bufio.Reader, msgs []string) error {
	for {
		line, err := b.ReadString('\n')
		if err != nil {
			for {
				stderr, err2 := i.stderr.ReadString('\n')
				if err2 != nil {
					break
				}
				fmt.Print(stderr)
			}
			return err
		}

		// Drop the clearing preamble as it makes it difficult to see output
		// when there's multiple emulator runs in a single binary.
		toPrint := line
		if strings.HasPrefix(toPrint, emuClearPrefix) {
			toPrint = toPrint[len(emuClearPrefix):]
		}
		fmt.Print(toPrint)
		for i, msg := range msgs {
			if strings.Contains(line, msg) {
				msgs = append(msgs[:i], msgs[i+1:]...)
				if len(msgs) == 0 {
					return nil
				}
				break
			}
		}
	}
}
