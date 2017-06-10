// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"log"
	"os"

	"application/lib/app/context"
	"fidl/bindings"

	ac "application/services/application_controller"
	al "application/services/application_launcher"
	sp "application/services/service_provider"

	"lib/fidl/examples/services/echo"
)

type EchoClientApp struct{}

func (client *EchoClientApp) Start(serverURL string) {
	context := context.CreateFromStartupInfo()

	echoProviderRequest, echoProviderPointer := sp.CreateChannelForServiceProvider()
	launchInfo := al.ApplicationLaunchInfo{
		Url:      serverURL,
		Services: &echoProviderRequest}
	applicationControllerRequest, applicationControllerPointer :=
		ac.CreateChannelForApplicationController()
	context.Launcher.CreateApplication(launchInfo, &applicationControllerRequest)

	echoProviderProxy := sp.NewServiceProviderProxy(echoProviderPointer, bindings.GetAsyncWaiter())

	echoRequest, echoPointer := echo.CreateChannelForEcho()
	echoProviderProxy.ConnectToService(echoRequest.Name(), echoRequest.PassChannel())
	echoProxy := echo.NewEchoProxy(echoPointer, bindings.GetAsyncWaiter())

	response, err := echoProxy.EchoString(bindings.StringPointer("Hello, Go world!"))
	if err != nil {
		log.Println(err)
	} else {
		fmt.Printf("client: %s\n", *response)
	}
	echoProxy.Close_Proxy()
	applicationControllerPointer.Close()
	context.Close()
}

func main() {
	serverURL := "file:///system/apps/echo_server_go"
	if len(os.Args) == 2 {
		serverURL = os.Args[1]
	}
	client := &EchoClientApp{}
	client.Start(serverURL)
}
