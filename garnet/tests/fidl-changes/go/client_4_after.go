// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fidl/fidl/test/after"
)

type client_after struct {
	addMethod    *after.AddMethodInterface
	removeMethod *after.RemoveMethodInterface
	addEvent     *after.AddEventInterface
	removeEvent  *after.RemoveEventInterface
}

func (c client_after) callAllMethodsExpectAllEvents() {
	c.addMethod.ExistingMethod()
	c.removeMethod.ExistingMethod()
	c.addMethod.NewMethod()
	_ = c.addEvent.ExpectNewEvent()
}
