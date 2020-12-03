// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	lib "fidl/fidl/test/addmethod"
	"syscall/zx/fidl"
)

type impl struct {
	lib.ExampleProtocolWithCtxTransitionalBase
}

func (*impl) ExistingMethod(fidl.Context) error {
	return nil
}

// Assert that the impl implements the ExampleProtocol interface
var _ lib.ExampleProtocolWithCtx = &impl{}

func main() {}
