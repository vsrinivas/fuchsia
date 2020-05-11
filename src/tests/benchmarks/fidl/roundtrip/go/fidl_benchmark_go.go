// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"log"
	"syscall/zx"
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

func main() {
	log.SetFlags(log.Lshortfile)

	ctx := component.NewContextFromStartupInfo()

	stub := benchmarks.BindingsUnderTestWithCtxStub{Impl: &impl{}}
	ctx.OutgoingService.AddService(
		benchmarks.BindingsUnderTestName,
		func(ctx fidl.Context, c zx.Channel) error {
			go component.ServeExclusive(ctx, &stub, c, func(err error) {
				log.Print(err)
			})
			return nil
		},
	)

	ctx.BindStartupHandle(context.Background())
}
