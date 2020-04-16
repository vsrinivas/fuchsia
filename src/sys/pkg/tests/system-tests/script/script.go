// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package script

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/exec"

	"fuchsia.googlesource.com/host_target_testing/device"
	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/sl4f"
)

// RunScript runs a script on the device and returns the result. It's possible
// this script could reboot the device, so to help with connection management,
// this function optionally takes a pointer to an sl4f.Client. If specified,
// this client will be closed, and the pointer will be updated to a new
// sl4f.Client after the script completes.
func RunScript(
	ctx context.Context,
	device *device.Client,
	repo *packages.Repository,
	rpcClient **sl4f.Client,
	script string,
) error {
	if script == "" {
		return nil
	}

	// It's possible the script could have rebooted the device, so
	// to be safe, disconnect from sl4f before running the script, then
	// reconnect afterwards.
	connectToSl4f := false
	if rpcClient != nil && *rpcClient != nil {
		(*rpcClient).Close()
		*rpcClient = nil
		connectToSl4f = true
	}

	log.Printf("running script: %s", script)
	cmd := exec.CommandContext(ctx, "/bin/sh", "-c", script)
	cmd.Env = os.Environ()
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("script %v failed to run: %w", err)
	}

	if connectToSl4f {
		rpc, err := device.StartRpcSession(ctx, repo)
		if err != nil {
			return fmt.Errorf("unable to connect to sl4f after running script: %w", err)
		}
		*rpcClient = rpc
	}

	return nil
}
