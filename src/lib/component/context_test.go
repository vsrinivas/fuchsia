// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package component_test

import (
	"context"
	"testing"

	"go.fuchsia.dev/fuchsia/src/lib/component"
)

// TODO(fxbug.dev/102390): This test does not pass in CFv2 because it assumes the outgoing dir is
// present in the startup handles, which is not true for tests run in the go test runner.
//
// This test is disabled to unblock CFv2 migration. Either rewrite the test, switch to
// elf_test_runner, or simply delete the test under the premise that it contributes limited value.
func _TestContext_BindStartupHandle(t *testing.T) {
	c := component.NewContextFromStartupInfo()

	if got := c.OutgoingService; got == nil {
		t.Fatal("got c.OutgoingService = nil, want non-nil")
	}

	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	c.BindStartupHandle(ctx)

	if got := c.OutgoingService; got != nil {
		t.Fatalf("got c.OutgoingService = %p, want nil", got)
	}
}
