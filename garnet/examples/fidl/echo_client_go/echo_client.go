// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"

	"app/context"
	"svc/services"

	echo "fidl/fidl/examples/echo"
	"fidl/fuchsia/sys"
)

type echoClientApp struct {
	ctx          *context.Context
	echoProvider *services.Provider
	controller   *sys.ComponentControllerInterface
	echo         *echo.EchoInterface
}

func (a *echoClientApp) startApplication(serverURL string) (li *sys.ComponentControllerInterface, err error) {
	pr, err := a.echoProvider.NewRequest()
	if err != nil {
		return nil, fmt.Errorf("NewRequest failed: %v", err)
	}
	defer func() {
		if err != nil {
			pr.Close()
		}
	}()

	launchInfo := sys.LaunchInfo{
		Url:              serverURL,
		DirectoryRequest: pr,
	}

	cr, cp, err := sys.NewComponentControllerInterfaceRequest()
	if err != nil {
		return nil, fmt.Errorf("NewComponentControllerInterfaceRequest failed: %v", err)
	}
	defer func() {
		if err != nil {
			cr.Close()
			cp.Close()
		}
	}()

	err = a.ctx.Launcher().CreateComponent(launchInfo, cr)
	if err != nil {
		return nil, fmt.Errorf("CreateComponent failed: %v", err)
	}
	return cp, nil
}

func (a *echoClientApp) getEchoInterface() (ei *echo.EchoInterface, err error) {
	r, p, err := echo.NewEchoInterfaceRequest()
	if err != nil {
		return nil, fmt.Errorf("NewEchoInterfaceRequest failed: %v", err)
	}
	defer func() {
		if err != nil {
			r.Close()
			p.Close()
		}
	}()

	err = a.echoProvider.ConnectToService(r)
	if err != nil {
		return nil, fmt.Errorf("ConnectToServiceAt failed: %v", err)
	}
	return p, nil
}

func main() {
	serverURL := flag.String("server", "fuchsia-pkg://fuchsia.com/echo_server_go#meta/echo_server_go.cmx", "server URL")
	msg := flag.String("m", "Hello, Go World", "message")

	flag.Parse()

	a := &echoClientApp{
		ctx:          context.CreateFromStartupInfo(),
		echoProvider: services.NewProvider(),
	}

	var err error
	a.controller, err = a.startApplication(*serverURL)
	if err != nil {
		fmt.Println(err)
		return
	}
	defer a.controller.Close()

	a.echo, err = a.getEchoInterface()
	if err != nil {
		fmt.Println(err)
		return
	}
	defer a.echo.Close()

	response, err := a.echo.EchoString(msg)
	if err != nil {
		fmt.Println("EchoString failed:", err)
		return
	}

	fmt.Println("client:", *response)
}
