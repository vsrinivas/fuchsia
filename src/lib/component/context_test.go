// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package component_test

import (
	"context"
	"testing"

	"go.fuchsia.dev/fuchsia/src/lib/component"
)

func TestContext_BindStartupHandle(t *testing.T) {
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
