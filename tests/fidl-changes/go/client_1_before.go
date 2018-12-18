// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fidl/fidl/test/before"
)

type client_before struct {
	addMethod    *before.AddMethodInterface
	removeMethod *before.RemoveMethodInterface
	addEvent     *before.AddEventInterface
	removeEvent  *before.RemoveEventInterface
}

func (c client_before) callAllMethodsExpectAllEvents() {
	c.addMethod.ExistingMethod()
	c.removeMethod.ExistingMethod()
	_ = c.removeEvent.ExpectOldEvent()
}
