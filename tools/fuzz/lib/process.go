// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"os"
	"os/exec"

	"github.com/golang/glog"
)

// ExecCommand is the function used by CreateProcess to create Cmd objects. Test code can replace
// the default to mock process creation.
var ExecCommand = exec.Command

// NewCommand returns a Cmd that can be used to start a new os.Process by calling Run or Start.
// It is provided to allow the test code to mock process creation.
func NewCommand(name string, args ...string) *exec.Cmd {
	cmd := ExecCommand(name, args...)
	glog.Infof("Running local command: %q", cmd)
	return cmd
}

// CreateProcessForeground is like CreateProcess but passes through any command output
func CreateProcessForeground(name string, args ...string) error {
	cmd := NewCommand(name, args...)
	cmd.Stderr, cmd.Stdout = os.Stderr, os.Stdout
	return cmd.Run()
}

// Run a command to completion
func CreateProcess(name string, args ...string) error {
	cmd := NewCommand(name, args...)
	return cmd.Run()
}
