// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"

	"application/lib/app/context"
	"fidl/bindings"

	"lib/fidl/examples/services/echo"
)

type EchoClientApp struct {
	ctx  *context.Context
	echo *echo.Proxy
}

func (a *EchoClientApp) Start() {
	r, p := echo.NewChannel()
	a.ctx.ConnectToEnvironmentService(r.Name(), bindings.InterfaceRequest(r))
	a.echo = echo.NewProxy(p, bindings.GetAsyncWaiter())

	resp, err := a.echo.EchoString(bindings.StringPointer("Hello, Go world!"))
	if err != nil {
		fmt.Println(err)
	} else {
		fmt.Printf("client: %s\n", *resp)
	}

	a.echo.Close()
}

func main() {
	a := &EchoClientApp{ctx: context.CreateFromStartupInfo()}
	a.Start()
}
