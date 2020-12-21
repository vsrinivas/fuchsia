// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	lib "fidl/fidl/test/protocolmethodadd"
	"syscall/zx/fidl"
)

// [START contents]
type client struct {
	addMethod *lib.ExampleWithCtxInterface
}

func (c client) test() {
	c.addMethod.ExistingMethod(context.Background())
	c.addMethod.NewMethod(context.Background())
}

type server struct{}

// Assert that server implements the Example interface
var _ lib.ExampleWithCtx = &server{}

func (*server) ExistingMethod(fidl.Context) error {
	return nil
}

func (*server) NewMethod(fidl.Context) error {
	return nil
}

// [END contents]

func main() {}
