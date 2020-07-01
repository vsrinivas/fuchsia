// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"log"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fuchsia/examples"
)

func main() {
	ctx := component.NewContextFromStartupInfo()

	// Connect to the EchoLauncher server
	echoLauncherReq, echoLauncher, err := examples.NewEchoLauncherWithCtxInterfaceRequest()
	if err != nil {
		log.Fatal("Could not create channel", err)
	}
	ctx.ConnectToEnvService(echoLauncherReq)

	// Obtaining a connection to an Echo protocol using the non-pipelined method
	{
		// The method responds with a client end of a channel already connected
		// to an Echo server
		echo, err := echoLauncher.GetEcho(context.Background(), "non pipelined: ")
		if err != nil {
			log.Fatal("Error making GetEcho call", err)
		}
		// We can use the response to make an EchoString request
		var response string
		response, err = echo.EchoString(context.Background(), "hello")
		if err != nil {
			log.Fatal("Error making EchoString call", err)
		}
		log.Println("Got echo response:", response)
	}

	// Obtaining a connection to an Echo protocol using the pipelined method
	{
		// Initialize the channel, and send the server end as part of the request
		echoReq, echo, err := examples.NewEchoWithCtxInterfaceRequest()
		if err != nil {
			log.Fatal("Could not create channel", err)
		}
		err = echoLauncher.GetEchoPipelined(context.Background(), "pipelined: ", echoReq)
		if err != nil {
			log.Fatal("Error sending GetEchoPipelined request", err)
		}
		// Immediately send requests against the server end
		var response string
		response, err = echo.EchoString(context.Background(), "hello")
		if err != nil {
			log.Fatal("Error making EchoString call", err)
		}
		log.Println("Got echo response:", response)
	}
}
