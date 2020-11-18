// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/bootserver"
	"go.fuchsia.dev/fuchsia/tools/botanist/target"
)

type mockTarget struct {
	target.DeviceTarget

	startErr error
}

func (t *mockTarget) Start(ctx context.Context, images []bootserver.Image, args []string, _ string) error {
	return t.startErr
}

func TestRunAgainstTarget(t *testing.T) {
	startErr := fmt.Errorf("start failed before setting tftp field")
	devices := []deviceTarget{
		&mockTarget{startErr: startErr},
	}
	var cmd ZedbootCommand
	err := cmd.runAgainstTargets(context.Background(), devices, []bootserver.Image{}, []string{})
	if err == nil || err != startErr {
		t.Fatalf("failed to return correct error")
	}
}
