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
	// IsolateNetwork indicates whether we should use a network namespace.
	IsolateNetwork bool
	// MountPoints is a list of locations on the current filesystem that
	// should be mounted into the NsJail.
	MountPoints []*MountPt
}

// Build takes the given subcmd and wraps it in the appropriate NsJail invocation.
func (n *NsJailCmdBuilder) Build(subcmd []string) ([]string, error) {
	if n.Bin == "" {
		return nil, errors.New("NsJailCmdBuilder: Bin cannot be empty")
	} else if len(subcmd) == 0 {
		return nil, errors.New("NsJailCmdBuilder: subcmd cannot be empty")
	}
	cmd := []string{n.Bin, "--keep_env"}
	if !n.IsolateNetwork {
		cmd = append(cmd, "--disable_clone_newnet")
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
	cmd = append(cmd, "--")
	cmd = append(cmd, subcmd...)
	return cmd, nil
}
