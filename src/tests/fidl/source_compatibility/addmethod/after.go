// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"syscall/zx/fidl"

	lib "fidl/fidl/test/addmethod"
)

type client struct {
	addMethod *lib.ExampleWithCtxInterface
}

func (c client) test() {
	c.addMethod.ExistingMethod(context.Background())
}

type server struct{}

// Assert that server implements the Example interface
var _ lib.ExampleWithCtx = &server{}

func (_ *server) ExistingMethod(fidl.Context) error {
	return nil
}
func (_ *server) NewMethod(fidl.Context) error {
	return nil
}

func main() {}
