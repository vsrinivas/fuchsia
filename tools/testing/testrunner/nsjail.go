// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testrunner

import (
	"errors"
	"fmt"
)

// MountPt describes the source, destination, and permissions for a
// mount point. This currently only supports bind mounts.
type MountPt struct {
	// Src is the path on the root filesystem to mount.
	Src string
	// Dst is the path inside the NsJail. If empty, we mount at Src.
	Dst string
	// Writable sets the mount points to rw (default is readonly).
	Writable bool
}

// NsJailCmdBuilder facilitates the construction of an NsJail command line
// invocation. See https://github.com/google/nsjail for NsJail docs.
type NsJailCmdBuilder struct {
	// Bin is the path to the NsJail binary. This is a required parameter.
	Bin string
	// Chroot is the path to a directory to set as the chroot.
	Chroot string
	// Cwd is the path to a directory to set as the working directory.
	// Note that this does not add any required mount points, the caller
	// is responsible for ensuring that the directory exists in the jail.
	Cwd string
	// Env is a set of additional environment variables to pass into
	// the jail. Takes the form of a list of key=value strings.
	// This set of environment variables is appended to the calling process'
	// environment, and overrides any duplicate values
	Env []string
	// IsolateNetwork indicates whether we should use a network namespace.
	IsolateNetwork bool
	// MountPoints is a list of locations on the current filesystem that
	// should be mounted into the NsJail.
	MountPoints []*MountPt
}

// AddDefaultMounts adds a set of mounts used by existing host tests.
// This is effectively an allowlist of mounts used by existing tests.
// Adding to this should be avoided if possible.
func (n *NsJailCmdBuilder) AddDefaultMounts() {
	n.MountPoints = append(n.MountPoints, []*MountPt{
		// Many host tests run emulators, which requires KVM.
		{Src: "/dev/kvm"},
		// Many host tests rely on /bin/bash or /bin/sh.
		{Src: "/bin/bash"},
		{Src: "/bin/sh"},
		// /bin/bash, in turn, is dynamically linked and requires that we mount the
		// system linker.
		{Src: "/lib"},
		{Src: "/lib64"},
		// Linux utilities used by a variety of tests.
		{Src: "/usr/bin/awk"},
		{Src: "/usr/bin/basename"},
		{Src: "/usr/bin/cat"},
		{Src: "/usr/bin/chmod"},
		{Src: "/usr/bin/cp"},
		{Src: "/usr/bin/cut"},
		{Src: "/usr/bin/dirname"},
		{Src: "/usr/bin/env"},
		{Src: "/usr/bin/find"},
		{Src: "/usr/bin/head"},
		{Src: "/usr/bin/ln"},
		{Src: "/usr/bin/mkdir"},
		{Src: "/usr/bin/realpath"},
		{Src: "/usr/bin/rm"},
		{Src: "/usr/bin/sed"},
		{Src: "/usr/bin/sort"},
		{Src: "/usr/bin/tee"},
		{Src: "/usr/bin/touch"},
		{Src: "/usr/bin/uname"},
		// Additional mounts for convenience.
		{Src: "/dev/urandom"},
		{Src: "/dev/zero"},
		{Src: "/dev/null", Writable: true},
	}...)
}

// Build takes the given subcmd and wraps it in the appropriate NsJail invocation.
func (n *NsJailCmdBuilder) Build(subcmd []string) ([]string, error) {
	// Validate the command.
	if n.Bin == "" {
		return nil, errors.New("NsJailCmdBuilder: Bin cannot be empty")
	} else if len(subcmd) == 0 {
		return nil, errors.New("NsJailCmdBuilder: subcmd cannot be empty")
	}

	// Nsjail has a chroot flag but unfortunately it mounts the root
	// readonly, so we get around this by mounting it manually as
	// writable. Additionally, this must be mounted first to allow
	// for sub-mounts.
	if n.Chroot != "" {
		n.MountPoints = append(
			[]*MountPt{
				{
					Src:      n.Chroot,
					Dst:      "/",
					Writable: true,
				},
			}, n.MountPoints...,
		)
	}

	// Build the actual command invocation.
	cmd := []string{n.Bin, "--keep_env"}
	if !n.IsolateNetwork {
		cmd = append(cmd, "--disable_clone_newnet")
	}
	if n.Cwd != "" {
		cmd = append(cmd, "--cwd", n.Cwd)
	}
	for _, mountPt := range n.MountPoints {
		if mountPt.Src == "" {
			return nil, errors.New("NsJailCmdBuilder: Src cannot be empty in a mount point")
		}
		if mountPt.Dst == "" {
			mountPt.Dst = mountPt.Src
		}
		if mountPt.Writable {
			cmd = append(cmd, "--bindmount", fmt.Sprintf("%s:%s", mountPt.Src, mountPt.Dst))
		} else {
			cmd = append(cmd, "--bindmount_ro", fmt.Sprintf("%s:%s", mountPt.Src, mountPt.Dst))

		}
	}

	// Remove some default rlimits as our emulator tests write large files
	// and allocate a large amount of RAM.
	cmd = append(
		cmd,
		"--rlimit_as", "inf",
		"--rlimit_fsize", "inf",
	)

	for _, v := range n.Env {
		cmd = append(cmd, "--env", v)
	}
	cmd = append(cmd, "--")
	cmd = append(cmd, subcmd...)
	return cmd, nil
}
