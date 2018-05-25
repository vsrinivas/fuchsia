// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"

	"app/context"
	"svc/services"

	"fidl/component"
	"fidl/echo2"
)

type echoClientApp struct {
	ctx          *context.Context
	echoProvider *services.Provider
	controller   *component.ComponentControllerInterface
	echo         *echo2.EchoInterface
}

func (a *echoClientApp) startApplication(serverURL string) (li *component.ComponentControllerInterface, err error) {
	pr, err := a.echoProvider.NewRequest()
	if err != nil {
		return nil, fmt.Errorf("NewRequest failed: %v", err)
	}
	defer func() {
		if err != nil {
			pr.Close()
		}
	}()

	launchInfo := component.LaunchInfo{
		Url:              serverURL,
		DirectoryRequest: pr,
	}

	cr, cp, err := component.NewComponentControllerInterfaceRequest()
	if err != nil {
		return nil, fmt.Errorf("NewComponentControllerInterfaceRequest failed: %v", err)
	}
	defer func() {
		if err != nil {
			cr.Close()
			cp.Close()
		}
	}()

	err = a.ctx.Launcher.CreateApplication(launchInfo, cr)
	if err != nil {
		return nil, fmt.Errorf("CreateApplication failed: %v", err)
	}
	return cp, nil
}

func (a *echoClientApp) getEchoInterface() (ei *echo2.EchoInterface, err error) {
	r, p, err := echo2.NewEchoInterfaceRequest()
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
	serverURL := flag.String("server", "echo2_server_go", "server URL")
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
