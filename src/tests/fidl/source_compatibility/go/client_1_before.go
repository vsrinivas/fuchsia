// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	"context"

	"fidl/fidl/test/before"
)

type client_before struct {
	addMethod    *before.AddMethodWithCtxInterface
	removeMethod *before.RemoveMethodWithCtxInterface
	addEvent     *before.AddEventWithCtxInterface
	removeEvent  *before.RemoveEventWithCtxInterface
}

func (c client_before) callAllMethodsExpectAllEvents() {
	c.addMethod.ExistingMethod(context.Background())
	c.removeMethod.ExistingMethod(context.Background())
	_ = c.removeEvent.ExpectOldEvent(context.Background())
}
