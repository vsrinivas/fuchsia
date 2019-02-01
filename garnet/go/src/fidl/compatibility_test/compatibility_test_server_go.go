// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"app/context"
	"fidl/fidl/test/compatibility"
	"fidl/fuchsia/sys"
	"fmt"
	"log"
	"svc/services"
	"syscall/zx"
	"syscall/zx/fidl"
)

type echoClientApp struct {
	ctx          *context.Context
	echoProvider *services.Provider
	controller   *sys.ComponentControllerInterface
	echo         *compatibility.EchoInterface
}

func (app *echoClientApp) startApplication(
	serverURL string) (*sys.ComponentControllerInterface, error) {
	directoryReq, err := app.echoProvider.NewRequest()
	if err != nil {
		return nil, fmt.Errorf("NewRequest failed: %v", err)
	}
	defer func() {
		if err != nil {
			directoryReq.Close()
		}
	}()

	launchInfo := sys.LaunchInfo{
		Url:              serverURL,
		DirectoryRequest: directoryReq,
	}

	req, pxy, err := sys.NewComponentControllerInterfaceRequest()
	if err != nil {
		return nil, fmt.Errorf(
			"NewComponentControllerInterfaceRequest failed: %v", err)
	}
	defer func() {
		if err != nil {
			req.Close()
			pxy.Close()
		}
	}()

	err = app.ctx.Launcher().CreateComponent(launchInfo, req)
	if err != nil {
		return nil, fmt.Errorf("CreateComponent failed: %v", err)
	}
	return pxy, nil
}

func (app *echoClientApp) getEchoInterface() (ei *compatibility.EchoInterface, err error) {
	req, pxy, err := compatibility.NewEchoInterfaceRequest()
	if err != nil {
		return nil, fmt.Errorf("NewEchoInterfaceRequest failed: %v", err)
	}
	defer func() {
		if err != nil {
			req.Close()
			pxy.Close()
		}
	}()

	err = app.echoProvider.ConnectToService(req)
	if err != nil {
		return nil, fmt.Errorf("ConnectToServiceAt failed: %v", err)
	}
	return pxy, nil
}

type echoImpl struct {
	ctx *context.Context
}

func (echo *echoImpl) EchoStruct(
	value compatibility.Struct, forward_to_server string) (compatibility.Struct, error) {
	if forward_to_server != "" {
		app := &echoClientApp{
			ctx:          echo.ctx,
			echoProvider: services.NewProvider(),
		}
		var err error
		app.controller, err = app.startApplication(forward_to_server)
		if err != nil {
			log.Println(err)
			return value, err
		}
		defer app.controller.Close()
		app.echo, err = app.getEchoInterface()
		if err != nil {
			log.Println(err)
			return value, err
		}
		defer app.echo.Close()
		response, err := app.echo.EchoStruct(value, "")
		if err != nil {
			log.Println("EchoStruct failed: ", err)
			return value, err
		}
		return response, nil
	} else {
		return value, nil
	}
}

func (echo *echoImpl) EchoStructNoRetVal(
	value compatibility.Struct, forward_to_server string) error {
	if forward_to_server != "" {
		app := &echoClientApp{
			ctx:          echo.ctx,
			echoProvider: services.NewProvider(),
		}
		var err error
		app.controller, err = app.startApplication(forward_to_server)
		if err != nil {
			log.Println(err)
			return err
		}
		app.echo, err = app.getEchoInterface()
		if err != nil {
			log.Println(err)
			return err
		}
		go app.listen()
		app.echo.EchoStructNoRetVal(value, "")
	} else {
		for _, key := range echoService.BindingKeys() {
			if pxy, ok := echoService.EventProxyFor(key); ok {
				pxy.EchoEvent(value)
			}
		}
	}
	return nil
}

func (app *echoClientApp) listen() {
	defer app.controller.Close()
	defer app.echo.Close()
	for {
		value, err := app.echo.ExpectEchoEvent()
		if err != nil {
			log.Println("ExpectEchoEvent failed: ", err)
			continue
		}
		for _, key := range echoService.BindingKeys() {
			if pxy, ok := echoService.EventProxyFor(key); ok {
				pxy.EchoEvent(value)
			}
		}
		break
	}
}

var echoService *compatibility.EchoService

func main() {
	echoService = &compatibility.EchoService{}
	ctx := context.CreateFromStartupInfo()
	ctx.OutgoingService.AddService(compatibility.EchoName,
		func(c zx.Channel) error {
			_, err := echoService.Add(&echoImpl{ctx: ctx}, c, nil)
			return err
		})
	go fidl.Serve()
	select {}
}
