// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	"context"
	"log"
	"os"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fidl/examples/echo"
)

type echoImpl struct {
	quiet bool
}

func (echo *echoImpl) EchoString(_ fidl.Context, inValue *string) (outValue *string, err error) {
	if !echo.quiet {
		log.Printf("server: %s\n", *inValue)
	}
	return inValue, nil
}

func main() {
	log.SetFlags(log.Lshortfile)

	quiet := (len(os.Args) > 1) && os.Args[1] == "-q"

	ctx := component.NewContextFromStartupInfo()

	stub := echo.EchoWithCtxStub{Impl: &echoImpl{quiet: quiet}}
	ctx.OutgoingService.AddService(
		echo.EchoName,
		func(ctx fidl.Context, c zx.Channel) error {
			go component.ServeExclusive(ctx, &stub, c, func(err error) {
				log.Print(err)
			})
			return nil
		},
	)

	ctx.BindStartupHandle(context.Background())
}
