// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pave

import (
	"context"
	"fmt"
	"log"
	"time"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/artifacts"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/device"
)

func PaveDevice(
	ctx context.Context,
	d *device.Client,
	build artifacts.Build,
	otaToRecovery bool,
) error {
	log.Printf("Starting to pave device")
	startTime := time.Now()

	mode := device.RebootToRecovery
	if otaToRecovery {
		mode = device.OTAToRecovery
	}

	if err := d.Pave(ctx, build, mode); err != nil {
		return fmt.Errorf("device failed to pave: %w", err)
	}

	log.Printf("Paving successful in %s", time.Now().Sub(startTime))

	return nil
}
