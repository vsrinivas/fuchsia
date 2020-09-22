// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"bufio"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"io/ioutil"
	"net"
	"os"
	"os/exec"
	"path"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/golang/glog"
	"go.fuchsia.dev/fuchsia/tools/qemu"
)

// A Launcher manages the lifecycle of an instance; e.g. starting and stopping
type Launcher interface {
	// Do any preparation necessary before starting. This is idempotent, and
	// does not need to be explicitly called by the client, as it will be
	// automatically called by Start as necessary.
	Prepare() error

	// Starts the instance, returning a Connector that can be used to communicate with it
	Start() (Connector, error)

	// Returns true iff the instance is running.
	IsRunning() (bool, error)

	// Stops the instance. This is allowed to take up to 3 seconds to return.
	Kill() error
}

const successfulBootMarker = "{{{reset}}}"
const qemuBootTimeout = 10 * time.Second

// A QemuLauncher starts Fuchsia on QEMU
type QemuLauncher struct {
	Pid    int
	TmpDir string

	build Build

	// Paths to files that are created by Prepare():
	initrd      string
	extendedBlk string

	// Overridable for testing
	timeout time.Duration
}

// NewQemuLauncher constructs a new QemuLauncher
func NewQemuLauncher(build Build) *QemuLauncher {
	return &QemuLauncher{build: build, timeout: qemuBootTimeout}
}

// Find an unused TCP port on the host
func getFreePort() (int, error) {
	listener, err := net.Listen("tcp", "localhost:0")
	if err != nil {
		return 0, err
	}

	port := listener.Addr().(*net.TCPAddr).Port

	// Technically this might get used before QEMU starts...
	listener.Close()
	return port, nil
}

// Configure QEMU appropriately
func getQemuInvocation(binary, kernel, initrd, blk string, port int) ([]string, error) {
	qemuCmd := &qemu.QEMUCommandBuilder{}

	qemuCmd.SetBinary(binary)
	qemuCmd.SetTarget(qemu.TargetEnum.X86_64, true /* KVM */)
	qemuCmd.SetKernel(kernel)
	qemuCmd.SetInitrd(initrd)

	qemuCmd.SetCPUCount(4)
	qemuCmd.SetMemory(3072 /* MiB */)

	qemuCmd.AddVirtioBlkPciDrive(qemu.Drive{
		ID:   "d0",
		File: blk,
	})

	qemuCmd.AddNetwork(qemu.Netdev{
		ID:  "net0",
		MAC: "52:54:00:63:5e:7b",
		User: &qemu.NetdevUser{
			Network:   "192.168.3.0/24",
			DHCPStart: "192.168.3.9",
			Host:      "192.168.3.2",
			Forwards:  []qemu.Forward{{HostPort: port, GuestPort: 22}},
		},
	})

	// The system will halt on a kernel panic instead of rebooting.
	qemuCmd.AddKernelArg("kernel.halt-on-panic=true")
	// Do not print colors.
	qemuCmd.AddKernelArg("TERM=dumb")
	// Necessary to redirect serial to stdout for x86.
	qemuCmd.AddKernelArg("kernel.serial=legacy")

	entropy := make([]byte, 32)
	if _, err := rand.Read(entropy); err == nil {
		qemuCmd.AddKernelArg("kernel.entropy-mixin=" + hex.EncodeToString(entropy))
	}

	return qemuCmd.Build()
}

func fileExists(path string) bool {
	_, err := os.Stat(path)
	return !os.IsNotExist(err)
}

// Prepare files that are needed by QEMU, if they haven't already been prepared
func (q *QemuLauncher) Prepare() error {
	// Create a tmpdir to store files we need
	// TODO(fxb/45431): If we fail to boot and give the user a handle, tempdirs will
	// get orphaned; be better about removing them on error
	if q.TmpDir == "" {
		tmpDir, err := ioutil.TempDir("", "clusterfuchsia-qemu-")
		if err != nil {
			return fmt.Errorf("Error creating tempdir: %s", err)
		}
		q.TmpDir = tmpDir
	}

	paths, err := q.build.Path("zbi", "fvm", "blk", "authkeys", "zbitool")
	if err != nil {
		return fmt.Errorf("Error resolving qemu dependencies: %s", err)
	}
	zbi, fvm, blk, authkeys, zbitool := paths[0], paths[1], paths[2], paths[3], paths[4]

	// Stick our SSH key into the authorized_keys files
	initrd := path.Join(q.TmpDir, "ssh-"+path.Base(zbi))
	if !fileExists(initrd) {
		// TODO(fxb/45424): generate ssh key per-instance
		entry := "data/ssh/authorized_keys=" + authkeys
		if err := CreateProcessForeground(zbitool, "-o", initrd, zbi, "-e", entry); err != nil {
			return fmt.Errorf("adding ssh key failed: %s", err)
		}
	}
	q.initrd = initrd

	// Make an expanded copy of the disk image
	extendedBlk := path.Join(q.TmpDir, "extended-"+path.Base(blk))
	if !fileExists(extendedBlk) {
		if err := CreateProcessForeground("cp", blk, extendedBlk); err != nil {
			return fmt.Errorf("cp failed: %s", err)
		}
		if err := CreateProcessForeground(fvm, extendedBlk, "extend",
			"--length", "3G"); err != nil {
			return fmt.Errorf("fvm failed: %s", err)
		}
	}
	q.extendedBlk = extendedBlk

	return nil
}

// Start launches QEMU and waits for it to get through the basic boot sequence.
// Note that networking will not necessarily be fully up by the time Start() returns
func (q *QemuLauncher) Start() (Connector, error) {
	running, err := q.IsRunning()
	if err != nil {
		return nil, fmt.Errorf("Error checking run state: %s", err)
	}

	if running {
		return nil, fmt.Errorf("Start called but already running")
	}

	if err := q.Prepare(); err != nil {
		return nil, fmt.Errorf("Error while preparing to start: %s", err)
	}

	paths, err := q.build.Path("qemu", "kernel", "sshid")
	if err != nil {
		return nil, fmt.Errorf("Error resolving qemu dependencies: %s", err)
	}
	binary, kernel, sshid := paths[0], paths[1], paths[2]

	port, err := getFreePort()
	if err != nil {
		return nil, fmt.Errorf("couldn't get free port: %s", err)
	}

	invocation, err := getQemuInvocation(binary, kernel, q.initrd, q.extendedBlk, port)
	if err != nil {
		return nil, fmt.Errorf("qemu configuration error: %s", err)
	}

	cmd := NewCommand(invocation[0], invocation[1:]...)

	// TODO(fxb/45431): log qemu output to some global tempfile to match current CF behavior

	outPipe, err := cmd.StdoutPipe()
	if err != nil {
		return nil, fmt.Errorf("error attaching stdout: %s", err)
	}
	cmd.Stderr = cmd.Stdout // capture stderr too

	errCh := make(chan error)
	go func() {
		// Save early log in case of error
		var log []string

		scanner := bufio.NewScanner(outPipe)
		for scanner.Scan() {
			line := scanner.Text()
			log = append(log, line)
			if strings.Contains(line, successfulBootMarker) {
				// Early boot has finished
				errCh <- nil
				return
			}
		}

		if err := scanner.Err(); err != nil {
			errCh <- fmt.Errorf("failed during scan: %s", err)
			return
		}

		// Dump the boot log
		for _, s := range log {
			glog.Errorf(s)
		}

		errCh <- fmt.Errorf("qemu exited early")
	}()

	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("failed to start qemu: %s", err)
	}

	q.Pid = cmd.Process.Pid

	glog.Info("Waiting for boot to complete...")

	select {
	case err := <-errCh:
		if err != nil {
			return nil, fmt.Errorf("error during boot: %s", err)
		}
	case <-time.After(q.timeout):
		// TODO(fxb/45431): kill/cleanup?
		return nil, fmt.Errorf("timeout waiting for boot")
	}

	glog.Info("Instance started.")

	// Detach from the child, since we will never wait on it
	cmd.Process.Release()

	return &SSHConnector{Host: "localhost", Port: port, Key: sshid}, nil
}

// IsRunning checks if the qemu process is alive
func (q *QemuLauncher) IsRunning() (bool, error) {
	if q.Pid == 0 {
		return false, nil
	}

	if err := CreateProcess("ps", "-p", strconv.Itoa(q.Pid)); err != nil {
		if cmderr, ok := err.(*exec.ExitError); ok && cmderr.ExitCode() == 1 {
			return false, nil
		}
		return false, fmt.Errorf("error running ps: %s", err)
	}

	return true, nil
}

// Kill tells the QEMU process to terminate, and cleans up the TmpDir
func (q *QemuLauncher) Kill() error {
	if q.Pid != 0 {
		glog.Infof("Killing PID %d", q.Pid)

		// TODO(fxb/45431): More gracefully, with timeout
		if err := syscall.Kill(q.Pid, syscall.SIGKILL); err != nil {
			glog.Warningf("failed to kill instance: %s", err)
		}

		q.Pid = 0
	}

	if err := os.RemoveAll(q.TmpDir); err != nil {
		glog.Warningf("failed to remove temp dir: %s", err)
	}

	return nil
}

func loadLauncherFromHandle(build Build, handle Handle) (Launcher, error) {
	// TODO(fxb/47479): detect launcher type
	launcher := NewQemuLauncher(build)

	if err := handle.PopulateObject(&launcher); err != nil {
		return nil, err
	}

	if launcher.Pid == 0 {
		return nil, fmt.Errorf("pid not found in handle")
	}

	if launcher.TmpDir == "" {
		return nil, fmt.Errorf("tmpdir not found in handle")
	}

	return launcher, nil
}
