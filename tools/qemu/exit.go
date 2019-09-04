// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
	"fmt"
	"log"
	"os/exec"
	"syscall"
)

// QEMUSuccessCodes encodes all possible QEMU exit codes that signify successful shutdown.
var qemuSuccessCodes = map[int]bool{
	0: true,

	// QEMU returns 31 to signify a graceful shutdown when running Zircon's core-tests on
	// x64 architecture. It writes a value to a specially declared I/O port, which is used
	// to compute the return code as `(<value> << 1) | 1` (always odd and non-zero):
	// 31 is that magic return code.
	31: true,
}

// CheckExitCode checks whether a given error encodes a successful QEMU exit code.
func CheckExitCode(err error) error {
	if err == nil {
		return nil
	}

	if exitErr, ok := err.(*exec.ExitError); ok {
		// This works on both Unix and Windows. Although the syscall package is generally
		// platform dependent, WaitStatus has a void-to-int ExitStatus() method in both cases.
		if status, ok := exitErr.Sys().(syscall.WaitStatus); ok {
			exitCode := status.ExitStatus()
			_, ok = qemuSuccessCodes[exitCode]
			if ok {
				log.Printf("successful QEMU exit status: %d", exitCode)
				return nil
			} else {
				return fmt.Errorf("unsuccessful QEMU exit status: %d", exitCode)
			}
		} else {
			return fmt.Errorf("could not derive exit code from associated os.ProcessState: %v", err)
		}
	} else {
		return fmt.Errorf("provided error is not of type *exec.ExitError: %v", err)
	}
}
