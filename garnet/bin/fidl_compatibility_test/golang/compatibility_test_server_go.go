// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	"syscall/zx/io"

	"app/context"

	"fidl/fidl/test/compatibility"
	"fidl/fuchsia/sys"
)

var _ compatibility.Echo = (*echoImpl)(nil)

type echoImpl struct {
	ctx *context.Context
}

func (echo *echoImpl) getServer(url string) (*compatibility.EchoInterface, error) {
	directoryReq, directoryInterface, err := io.NewDirectoryInterfaceRequest()
	if err != nil {
		return nil, err
	}
	launchInfo := sys.LaunchInfo{
		Url:              url,
		DirectoryRequest: directoryReq.Channel,
	}

	componentControllerReq, _, err := sys.NewComponentControllerInterfaceRequest()
	if err != nil {
		return nil, err
	}
	if err := echo.ctx.Launcher().CreateComponent(launchInfo, componentControllerReq); err != nil {
		return nil, err
	}

	echoReq, echoInterface, err := compatibility.NewEchoInterfaceRequest()
	if err != nil {
		return nil, err
	}
	if err := fdio.ServiceConnectAt(zx.Handle(directoryInterface.Channel), echoReq.Name(), zx.Handle(echoReq.Channel)); err != nil {
		return nil, err
	}
	return echoInterface, nil
}

func (echo *echoImpl) EchoStruct(value compatibility.Struct, forwardURL string) (compatibility.Struct, error) {
	if forwardURL != "" {
		echoInterface, err := echo.getServer(forwardURL)
		if err != nil {
			log.Printf("Connecting to %s failed: %s", forwardURL, err)
			return compatibility.Struct{}, err
		}
		response, err := echoInterface.EchoStruct(value, "")
		if err != nil {
			log.Printf("EchoStruct failed: %s", err)
			return compatibility.Struct{}, err
		}
		return response, nil
	}

	return value, nil
}

func (echo *echoImpl) EchoStructNoRetVal(value compatibility.Struct, forwardURL string) error {
	if forwardURL != "" {
		echoInterface, err := echo.getServer(forwardURL)
		if err != nil {
			log.Printf("Connecting to %s failed: %s", forwardURL, err)
			return err
		}
		go func() {
			for {
				value, err := echoInterface.ExpectEchoEvent()
				if err != nil {
					log.Printf("ExpectEchoEvent failed: %s", err)
					continue
				}
				for _, key := range echoService.BindingKeys() {
					if pxy, ok := echoService.EventProxyFor(key); ok {
						pxy.EchoEvent(value)
					}
				}
				break
			}
		}()
		echoInterface.EchoStructNoRetVal(value, "")
	} else {
		for _, key := range echoService.BindingKeys() {
			if pxy, ok := echoService.EventProxyFor(key); ok {
				pxy.EchoEvent(value)
			}
		}
	}
	return nil
}

var echoService compatibility.EchoService

func main() {
	ctx := context.CreateFromStartupInfo()

	ctx.OutgoingService.AddService(compatibility.EchoName,
		func(c zx.Channel) error {
			_, err := echoService.Add(&echoImpl{ctx: ctx}, c, nil)
			return err
		})
	fidl.Serve()
}
