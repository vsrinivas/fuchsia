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

type EchoClientApp struct{}

func (client *EchoClientApp) Start() {
	context := context.CreateFromStartupInfo()

	echoRequest, echoPointer := echo.CreateChannelForEcho()
	context.ConnectToEnvironmentService(
		echoRequest.Name(), bindings.InterfaceRequest(echoRequest))
	echoProxy := echo.NewEchoProxy(echoPointer, bindings.GetAsyncWaiter())

	response, err :=
		echoProxy.EchoString(bindings.StringPointer("Hello, Go world!"))
	if err != nil {
		fmt.Println(err)
	} else {
		fmt.Printf("client: %s\n", *response)
	}
	echoProxy.Close_Proxy()
	context.Close()
}

func main() {
	client := &EchoClientApp{}
	client.Start()
}
