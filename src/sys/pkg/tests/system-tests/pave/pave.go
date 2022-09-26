// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pave

import (
	"context"
	"fmt"
	"time"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/artifacts"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/device"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

func PaveDevice(
	ctx context.Context,
	d *device.Client,
	build artifacts.Build,
) error {
	logger.Infof(ctx, "Starting to pave device")
	startTime := time.Now()

	if err := d.Pave(ctx, build); err != nil {
		return fmt.Errorf("device failed to pave: %w", err)
	}

	if err := d.Reconnect(ctx); err != nil {
		return fmt.Errorf("device failed to connect after pave: %w", err)
	}

	logger.Infof(ctx, "device booted")
	logger.Infof(ctx, "Paving successful in %s", time.Now().Sub(startTime))

	return nil
}
