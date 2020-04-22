// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"os"

	appcontext "app/context"

	"syscall/zx"
	"syscall/zx/dispatch"
	"syscall/zx/fidl"

	echo "fidl/fidl/examples/echo"
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
	quiet := (len(os.Args) > 1) && os.Args[1] == "-q"
	var echoService echo.EchoService
	c := appcontext.CreateFromStartupInfo()
	c.OutgoingService.AddService(
		echo.EchoName,
		&echo.EchoWithCtxStub{Impl: &echoImpl{quiet: quiet}},
		func(s fidl.Stub, c zx.Channel, ctx fidl.Context) error {
			d, ok := dispatch.GetDispatcher(ctx)
			if !ok {
				log.Fatal("no dispatcher provided on FIDL context")
			}
			_, err := echoService.BindingSet.AddToDispatcher(s, c, d, nil)
			return err
		},
	)
	d, err := dispatch.NewDispatcher()
	if err != nil {
		log.Fatalf("couldn't initialize FIDL dispatcher: %s", err)
	}
	c.BindStartupHandle(d)
	d.Serve()
}
