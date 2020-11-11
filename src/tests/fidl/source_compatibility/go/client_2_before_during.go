// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	"context"

	"fidl/fidl/test/during"
)

type client_before_during struct {
	addMethod    *during.AddMethodWithCtxInterface
	removeMethod *during.RemoveMethodWithCtxInterface
	addEvent     *during.AddEventWithCtxInterface
	removeEvent  *during.RemoveEventWithCtxInterface
}

func (c client_before_during) callAllMethodsExpectAllEvents() {
	c.addMethod.ExistingMethod(context.Background())
	c.removeMethod.ExistingMethod(context.Background())
	_ = c.removeEvent.ExpectOldEvent(context.Background())
}
