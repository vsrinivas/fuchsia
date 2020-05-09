// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"syscall/zx"
	"syscall/zx/dispatch"
	"syscall/zx/fidl"

	"fuchsia.googlesource.com/component"

	"fidl/fidl/benchmarks"
)

var _ benchmarks.BindingsUnderTestWithCtx = (*impl)(nil)

type impl struct{}

func (b *impl) EchoBytes(ctx fidl.Context, bytes []uint8) ([]uint8, error) {
	return bytes, nil
}

func (b *impl) EchoString(ctx fidl.Context, str string) (string, error) {
	return str, nil
}

func (b *impl) EchoStrings(ctx fidl.Context, strings []string) ([]string, error) {
	return strings, nil
}

func (b *impl) EchoHandles(ctx fidl.Context, handles []zx.Handle) ([]zx.Handle, error) {
	return handles, nil
}

var benchmarkService benchmarks.BindingsUnderTestService

func main() {
	ctx := component.NewContextFromStartupInfo()

	dispatcher, err := dispatch.NewDispatcher()
	if err != nil {
		panic("could not initialize FIDL dispatcher: " + err.Error())
	}
	ctx.OutgoingService.AddService(
		benchmarks.BindingsUnderTestName,
		&benchmarks.BindingsUnderTestWithCtxStub{Impl: &impl{}},
		func(s fidl.Stub, c zx.Channel, _ fidl.Context) error {
			_, err := benchmarkService.BindingSet.AddToDispatcher(s, c, dispatcher, nil)
			return err
		},
	)
	ctx.BindStartupHandle(dispatcher)
	dispatcher.Serve()
}
