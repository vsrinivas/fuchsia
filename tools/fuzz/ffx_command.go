// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"os/exec"
	"time"
)

// When running a local ffx command that effectively results in running a
// remote command on the Instance (i.e. a fuzzer), this wraps the resulting
// exec.Cmd to fit the InstanceCmd interface.
type FfxInstanceCmd struct {
	*exec.Cmd
}

func (c *FfxInstanceCmd) SetTimeout(duration time.Duration) {
	// TODO(fxbug.dev/106110): This is not currently used for fuzzer commands,
	// since timeouts are enforced by both libFuzzer and ClusterFuzz, and is
	// only used for non-fuzzer SSH instance commands. In the future it could
	// be implemented using exec.CommandContext, but may also simply be removed
	// from this interface after removing SSH support.
}

func (c *FfxInstanceCmd) Kill() error {
	return c.Process.Kill()
}
