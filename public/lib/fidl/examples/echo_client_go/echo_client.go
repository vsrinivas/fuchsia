// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"os"

	"app/context"
	"fidl/bindings"

	"lib/fidl/examples/services/echo"
)

type echoClientApp struct {
	ctx  *context.Context
	echo *echo.Echo_Proxy
}

func (a *echoClientApp) start(msg string) {
	r, p := a.echo.NewRequest(bindings.GetAsyncWaiter())
	a.echo = p
	a.ctx.ConnectToEnvService(r)

	resp, err := a.echo.EchoString(&msg)
	if err != nil {
		fmt.Println(err)
	} else {
		fmt.Printf("Response: %s\n", *resp)
	}

	a.echo.Close()
}

func main() {
	a := &echoClientApp{ctx: context.CreateFromStartupInfo()}
	if len(os.Args) > 1 {
		a.start(os.Args[1])
	} else {
		a.start("Hello, Go World")
	}
}
