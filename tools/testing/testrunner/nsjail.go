// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testrunner

import (
	"errors"
	"fmt"
	"sort"
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
	// UseTmpfs mounts Dst as a tmpfs.
	UseTmpfs bool
}

// NsJailCmdBuilder facilitates the construction of an NsJail command line
// invocation. See https://github.com/google/nsjail for NsJail docs.
type NsJailCmdBuilder struct {
	// Bin is the path to the NsJail binary. This is a required parameter.
	Bin string
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
		// Many host tests run emulators, which requires KVM and TUN/TAP.
		{Src: "/dev/kvm"},
		{Src: "/dev/net/tun"},
		// /bin/bash, in turn, is dynamically linked and requires that we mount the
		// system linker.
		{Src: "/lib"},
		{Src: "/lib64"},
		// A variety of tests use linux utilities from /usr/bin and /bin.
		// e.g. https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/testing/sl4f/client/test/sl4f_client_test.dart;l=69.
		{Src: "/usr/bin"},
		{Src: "/bin"},
		// Additional mounts for convenience.
		{Src: "/dev/urandom"},
		{Src: "/dev/zero"},
		{Src: "/dev/null", Writable: true},
		// Some host tests utilize ssh-keygen to generate key pairs, which reads
		// /etc/passwd to figure out the current username.
		{Src: "/etc/passwd"},
		// The Vulkan vkreadback host tests rely on libvulkan and mesa drivers
		// from the host, so mount the appropriate directories.
		{Src: "/usr/lib"},
		{Src: "/usr/share/vulkan"},
		// Some tests attempt to resolve domain names, which requires DNS config.
		{Src: "/etc/nsswitch.conf"},
		{Src: "/etc/resolv.conf"},
		// Some tests attempt to make https connections, which requires SSL
		// certs.
		{Src: "/etc/ssl/certs"},
		// Network conformance tests use TCL.
		{Src: "/usr/share/tcltk"},
		// netstack_streamsocket_c_api_test requires the presence of
		// /etc/host.conf because they call getaddrinfo on localhost.
		{Src: "/etc/host.conf"},
		// Some tests use awk, which then hops through a symlink into
		// /etc/alternatives to resolve to gawk.
		{Src: "/etc/alternatives/awk"},
		// fvdl_intree_test_tuntap reads /etc/hosts.
		{Src: "/etc/hosts"},
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

	// Build the actual command invocation.
	cmd := []string{n.Bin, "--keep_env"}
	if !n.IsolateNetwork {
		cmd = append(cmd, "--disable_clone_newnet")
	}
	if n.Cwd != "" {
		cmd = append(cmd, "--cwd", n.Cwd)
	}
	// Validate mount points and fill in any missing destinations.
	mountByDst := map[string]*MountPt{}
	var dsts []string
	for _, mountPt := range n.MountPoints {
		if mountPt.Src == "" && !mountPt.UseTmpfs {
			return nil, errors.New("NsJailCmdBuilder: Src can only be empty if using a tmpfs mount")
		}
		if mountPt.Dst == "" {
			mountPt.Dst = mountPt.Src
		}
		dsts = append(dsts, mountPt.Dst)
		mountByDst[mountPt.Dst] = mountPt
	}

	// Sort the mount destinations lexicographically to ensure that parent
	// directories are mounted before their children are.
	sort.Strings(dsts)

	// Add the mount points to the command.
	for _, dst := range dsts {
		mountPt := mountByDst[dst]
		if mountPt.Writable {
			cmd = append(cmd, "--bindmount", fmt.Sprintf("%s:%s", mountPt.Src, mountPt.Dst))
		} else if mountPt.UseTmpfs {
			cmd = append(cmd, "--tmpfsmount", mountPt.Dst)
		} else {
			cmd = append(cmd, "--bindmount_ro", fmt.Sprintf("%s:%s", mountPt.Src, mountPt.Dst))
		}
	}

	// Overwrite some default nsjail rlimits with our system soft maximums as
	// the defaults are too restrictive. We should probably tune this a bit
	// more in the future to absolute values.
	cmd = append(
		cmd,
		"--rlimit_as", "soft",
		"--rlimit_fsize", "soft",
		"--rlimit_nofile", "soft",
		"--rlimit_nproc", "soft",
	)

	for _, v := range n.Env {
		cmd = append(cmd, "--env", v)
	}
	cmd = append(cmd, "--")
	cmd = append(cmd, subcmd...)
	return cmd, nil
}
