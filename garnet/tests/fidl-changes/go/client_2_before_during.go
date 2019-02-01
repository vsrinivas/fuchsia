// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fidl/fidl/test/during"
)

type client_before_during struct {
	addMethod    *during.AddMethodInterface
	removeMethod *during.RemoveMethodInterface
	addEvent     *during.AddEventInterface
	removeEvent  *during.RemoveEventInterface
}

func (c client_before_during) callAllMethodsExpectAllEvents() {
	c.addMethod.ExistingMethod()
	c.removeMethod.ExistingMethod()
	_ = c.removeEvent.ExpectOldEvent()
}
