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

	// Initialize the server and client ends of the channel that will be used to
	// communicate with the Echo server
	echoServerEnd, echoClientEnd, err := examples.NewEchoWithCtxInterfaceRequest()
	if err != nil {
		log.Fatal("Could not create channel", err)
	}
	// Connect to the Echo server by asking it to bind to the specified channel end
	ctx.ConnectToEnvService(echoServerEnd)

	// Make an EchoString request and wait for the response
	response, err := echoClientEnd.EchoString(context.Background(), "hello")
	if err != nil {
		log.Fatal("Error making EchoString call", err)
	}
	log.Println("Got response:", response)

	// Make a SendString request
	err = echoClientEnd.SendString(context.Background(), "hi")
	if err != nil {
		log.Fatal("Error sending SendString request", err)
	}
	// Wait for an OnString event. If any other type of message is received,
	// this will error.
	response, err = echoClientEnd.ExpectOnString(context.Background())
	if err != nil {
		log.Fatal("Error expecting OnString event", err)
	}
	log.Println("Got event:", response)
}
