// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"syscall/zx/fidl"

	"fidl/fidl/test/after"
)

type client_after struct {
	addMethod    *after.AddMethodWithCtxInterface
	removeMethod *after.RemoveMethodWithCtxInterface
	addEvent     *after.AddEventWithCtxInterface
	removeEvent  *after.RemoveEventWithCtxInterface
}

func (c client_after) callAllMethodsExpectAllEvents() {
	c.addMethod.ExistingMethod(fidl.Background())
	c.removeMethod.ExistingMethod(fidl.Background())
	c.addMethod.NewMethod(fidl.Background())
	_ = c.addEvent.ExpectNewEvent(fidl.Background())
}
