// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"

	"fuchsia.googlesource.com/tools/build"
	"fuchsia.googlesource.com/tools/logger"
	"fuchsia.googlesource.com/tools/qemu"
	"fuchsia.googlesource.com/tools/secrets"
	"github.com/google/subcommands"
)

// QEMUBinPrefix is the prefix of the QEMU binary name, which is of the form
// qemu-system-<QEMU arch suffix>.
const qemuBinPrefix = "qemu-system"

// QEMUCommand is a Command implementation for running the testing workflow on an emulated
// target within QEMU.
type QEMUCommand struct {
	// ImageManifest is the path an image manifest specifying a QEMU kernel, a zircon
	// kernel, and block image to back QEMU storage.
	imageManifest string

	// QEMUBinDir is a path to a directory of QEMU binaries.
	qemuBinDir string

	// MinFSImage is a path to a minFS image to be mounted on target, and to where test
	// results will be written.
	minFSImage string

	// MinFSBlkDevPCIAddr is a minFS block device PCI address.
	minFSBlkDevPCIAddr string

	// TargetArch is the target architecture to be emulated within QEMU
	targetArch string

	// EnableKVM dictates whether to enable KVM.
	enableKVM bool

	// EnableNetworking dictates whether to enable external networking.
	enableNetworking bool
}

func (*QEMUCommand) Name() string {
	return "qemu"
}

func (*QEMUCommand) Usage() string {
	return "qemu [flags...] [kernel command-line arguments...]\n\nflags:\n"
}

func (*QEMUCommand) Synopsis() string {
	return "boots a QEMU device with a MinFS image as a block device."
}

func (cmd *QEMUCommand) SetFlags(f *flag.FlagSet) {
	f.StringVar(&cmd.imageManifest, "images", "", "path to an image manifest")
	f.StringVar(&cmd.qemuBinDir, "qemu-dir", "", "")
	f.StringVar(&cmd.minFSImage, "minfs", "", "path to minFS image")
	f.StringVar(&cmd.minFSBlkDevPCIAddr, "pci-addr", "06.0", "minFS block device PCI address")
	f.StringVar(&cmd.targetArch, "arch", "", "target architecture (x64 or arm64)")
	f.BoolVar(&cmd.enableKVM, "use-kvm", false, "whether to enable KVM")
	f.BoolVar(&cmd.enableNetworking, "enable-networking", false, "whether to enable external networking")
}

func (cmd *QEMUCommand) execute(ctx context.Context, cmdlineArgs []string) error {
	if cmd.qemuBinDir == "" {
		return fmt.Errorf("-qemu-dir must be set")
	}

	imgs, err := build.LoadImages(cmd.imageManifest)
	if err != nil {
		return err
	}

	qemuCPU, ok := map[string]string{
		"x64":   "x86_64",
		"arm64": "aarch64",
	}[cmd.targetArch]
	if !ok {
		return fmt.Errorf("cpu %q not recognized", cmd.targetArch)
	}
	qemuBinPath := filepath.Join(cmd.qemuBinDir, fmt.Sprintf("%s-%s", qemuBinPrefix, qemuCPU))

	cfg := qemu.Config{
		QEMUBin:        qemuBinPath,
		CPU:            cmd.targetArch,
		KVM:            cmd.enableKVM,
		MinFSImage:     cmd.minFSImage,
		PCIAddr:        cmd.minFSBlkDevPCIAddr,
		InternetAccess: cmd.enableNetworking,
	}

	// The system will halt on a kernel panic instead of rebooting
	cmdlineArgs = append(cmdlineArgs, "kernel.halt-on-panic=true")
	// Print a message if `dm poweroff` times out.
	cmdlineArgs = append(cmdlineArgs, "devmgr.suspend-timeout-debug=true")
	// Do not print colors.
	cmdlineArgs = append(cmdlineArgs, "TERM=dumb")
	if cmd.targetArch == "x64" {
		// Necessary to redirect to stdout.
		cmdlineArgs = append(cmdlineArgs, "kernel.serial=legacy")
	}

	invocation, err := qemu.CreateInvocation(cfg, imgs, cmdlineArgs)
	if err != nil {
		return err
	}

	// The QEMU command needs to be invoked within an empty directory, as QEMU will attempt
	// to pick up files from its working directory, one notable culprit being multiboot.bin.
	// This can result in strange behavior.
	qemuWorkingDir, err := ioutil.TempDir("", "qemu-working-dir")
	if err != nil {
		return err
	}
	defer os.RemoveAll(qemuWorkingDir)

	qemuCmd := exec.Cmd{
		Path:   invocation[0],
		Args:   invocation,
		Dir:    qemuWorkingDir,
		Stdout: os.Stdout,
		Stderr: os.Stderr,
	}
	logger.Debugf(ctx, "QEMU invocation:\n%s", invocation)
	return qemu.CheckExitCode(qemuCmd.Run())
}

func (cmd *QEMUCommand) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	// TODO(IN-607) Once the secrets pipeline is supported on hardware, move the starting
	// of the secrets server to botanist's main().
	//
	// The secrets server will start up iff LUCI_CONTEXT is set and contains secret bytes.
	secrets.StartSecretsServer(ctx, 8081)

	if err := cmd.execute(ctx, f.Args()); err != nil {
		logger.Errorf(ctx, "%v\n", err)
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}
