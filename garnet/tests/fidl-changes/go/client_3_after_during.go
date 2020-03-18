// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fidl/fidl/test/during"
	"syscall/zx/fidl"
)

type client_after_during struct {
	addMethod    *during.AddMethodWithCtxInterface
	removeMethod *during.RemoveMethodWithCtxInterface
	addEvent     *during.AddEventWithCtxInterface
	removeEvent  *during.RemoveEventWithCtxInterface
}

func (c client_after_during) callAllMethodsExpectAllEvents() {
	c.addMethod.ExistingMethod(fidl.Background())
	c.removeMethod.ExistingMethod(fidl.Background())
	c.addMethod.NewMethod(fidl.Background())
	_ = c.addEvent.ExpectNewEvent(fidl.Background())
}
