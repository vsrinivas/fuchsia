// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
	"context"
	"fmt"
	"os/exec"
	"path/filepath"
)

// QEMUBuilder is used to generate a command to create a QEMU instance.
type QEMUBuilder struct {
	// QEMUBinPath is an absolute path to the QEMU binary.
	qemuBinPath string

	// Args is an array of QEMU arguments.
	args []string
}

// Initialize sets the emulated machine and cpu types.
//
// This method must be called before any others.
func (q *QEMUBuilder) Initialize(qemuBinPath, targetArch string, enableKVM bool) error {
	absQEMUBinPath, err := filepath.Abs(qemuBinPath)
	if err != nil {
		return err
	}
	q.qemuBinPath = absQEMUBinPath

	switch targetArch {
	case "arm64":
		if enableKVM {
			q.AddArgs("-machine", "virt,gic_version=host")
			q.AddArgs("-cpu", "host")
			q.AddArgs("-enable-kvm")
		} else {
			q.AddArgs("-machine", "virt,gic_version=3")
			q.AddArgs("-machine", "virtualization=true")
			q.AddArgs("-cpu", "cortex-a53")
		}

	case "x64":
		q.AddArgs("-machine", "q35")
		// Necessary for userboot.shutdown to trigger properly, since it writes to
		// 0xf4 to debug-exit in QEMU.
		q.AddArgs("-device", "isa-debug-exit,iobase=0xf4,iosize=0x04")

		if enableKVM {
			q.AddArgs("-cpu", "host")
			q.AddArgs("-enable-kvm")
		} else {
			q.AddArgs("-cpu", "Haswell,+smap,-check,-fsgsbase")
		}

	default:
		return fmt.Errorf("target arch \"%s\" not recognized", targetArch)
	}

	return nil
}

// AddArgs sets QEMU arguments.
func (q *QEMUBuilder) AddArgs(args ...string) {
	q.args = append(q.args, args...)
}

// Cmd returns the associated QEMU invocation command. Once executed, the process may be
// canceled via the passed in context.
func (q *QEMUBuilder) Cmd(ctx context.Context) *exec.Cmd {
	return exec.CommandContext(ctx, q.qemuBinPath, q.args...)
}
