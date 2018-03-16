// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"

	"app/context"
	"fidl/bindings"
	"svc/services"

	"garnet/public/lib/app/fidl/application_controller"
	"garnet/public/lib/app/fidl/application_launcher"

	"garnet/examples/fidl/services/echo"
)

type echoClientApp struct {
	ctx          *context.Context
	echoProvider *services.Provider
	controller   *application_controller.ApplicationController_Pointer
	echo         *echo.Echo_Proxy
}

func (a *echoClientApp) start(serverURL string, msg string) {
	pr, err := a.echoProvider.NewRequest()
	if err != nil {
		fmt.Println(err)
		return
	}

	launchInfo := application_launcher.ApplicationLaunchInfo{
		Url:              serverURL,
		DirectoryRequest: pr}

	cr, cp := a.controller.NewRequest()
	a.ctx.Launcher.CreateApplication(launchInfo, cr)
	a.controller = cp
	defer a.controller.Close()

	r, p := a.echo.NewRequest(bindings.GetAsyncWaiter())
	a.echoProvider.ConnectToService(r)
	a.echo = p
	defer a.echo.Close()

	response, err := a.echo.EchoString(&msg)
	if err != nil {
		fmt.Println(err)
	} else {
		fmt.Println("client:", *response)
	}
}

func main() {
	serverURL := flag.String("server", "echo_server_go", "server URL")
	msg := flag.String("m", "Hello, Go World", "message")

	flag.Parse()

	a := &echoClientApp{
		ctx:          context.CreateFromStartupInfo(),
		echoProvider: services.NewProvider()}

	a.start(*serverURL, *msg)
}
