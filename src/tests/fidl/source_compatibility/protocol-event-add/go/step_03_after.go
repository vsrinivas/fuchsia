// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	lib "fidl/fidl/test/protocoleventadd"
)

// [START contents]
func expectEvents(c *lib.ExampleWithCtxInterface) {
	_ = c.ExpectOnExistingEvent(context.Background())
	_ = c.ExpectOnNewEvent(context.Background())
}

func sendEvents(p *lib.ExampleEventProxy) {
	_ = p.OnExistingEvent()
	_ = p.OnNewEvent()
}

// [END contents]

func main() {}
