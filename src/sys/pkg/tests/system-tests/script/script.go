// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package script

import (
	"context"
	"fmt"
	"os"
	"os/exec"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/device"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/packages"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/sl4f"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
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

	ch := device.DisconnectionListener()

	logger.Debugf(ctx, "running script: %s", script)
	cmd := exec.CommandContext(ctx, "/bin/sh", "-c", script)
	cmd.Env = os.Environ()
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("script %v failed to run: %w", script, err)
	}

	// Reconnect to sl4f if we disconnected.
	//
	// FIXME(47145) To avoid fxbug.dev/47145, we need to delay
	// disconnecting from sl4f until after we reboot the device. Otherwise
	// we risk leaving the ssh session in a bad state.
	if rpcClient != nil && *rpcClient != nil {
		select {
		case <-ch:
			var err error
			(*rpcClient).Close()
			*rpcClient, err = device.StartRpcSession(ctx, repo)
			if err != nil {
				return fmt.Errorf("unable to connect to sl4f after running script: %w", err)
			}
		default:
		}
	}

	return nil
}
