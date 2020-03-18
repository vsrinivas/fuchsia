// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"syscall/zx/fidl"

	"fidl/fidl/test/during"
)

type client_before_during struct {
	addMethod    *during.AddMethodWithCtxInterface
	removeMethod *during.RemoveMethodWithCtxInterface
	addEvent     *during.AddEventWithCtxInterface
	removeEvent  *during.RemoveEventWithCtxInterface
}

func (c client_before_during) callAllMethodsExpectAllEvents() {
	c.addMethod.ExistingMethod(fidl.Background())
	c.removeMethod.ExistingMethod(fidl.Background())
	_ = c.removeEvent.ExpectOldEvent(fidl.Background())
}
