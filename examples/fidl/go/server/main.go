// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	"context"
	"log"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fuchsia/examples"
)

// echoImpl implements the server functionality of the `Echo` protocol.
//
// It holds an `EchoEventProxy` in order to send events to the client.
type echoImpl struct {
	eventSender examples.EchoEventProxy
}

// Assert that `*echoImpl` implements the `EchoWithCtx` interface.
var _ examples.EchoWithCtx = (*echoImpl)(nil)

// EchoString implements the server-side `EchoString` FIDL method. It replies
// with the request value.
func (*echoImpl) EchoString(_ fidl.Context, inValue string) (string, error) {
	log.Println("Received EchoString request", inValue)
	return inValue, nil
}

// SendString implements the server-side `SendString` FIDL method. It sends an
// `OnString` event with the request value.
func (echo *echoImpl) SendString(_ fidl.Context, inValue string) error {
	log.Println("Received SendString request", inValue)
	if err := echo.eventSender.OnString(inValue); err != nil {
		log.Println("Failed to send event:", err)
	}
	// We choose to return nil instead of the event sender error, since we don't
	// want the server to keep running even if it couldn't send the event.
	return nil
}

func main() {
	log.SetFlags(log.Lshortfile)

	ctx := component.NewContextFromStartupInfo()

	ctx.OutgoingService.AddService(
		// Register the service under EchoName. This is the same name that
		// clients will refer to when they connect using ConnectToEnvService
		examples.EchoName,
		// The handler for incoming requests for connecting to Echo. It serves
		// our implementation of Echo on the provided channel
		func(ctx fidl.Context, c zx.Channel) error {
			stub := examples.EchoWithCtxStub{
				Impl: &echoImpl{
					eventSender: examples.EchoEventProxy(fidl.ChannelProxy{Channel: c}),
				},
			}
			go component.ServeExclusive(ctx, &stub, c, func(err error) {
				log.Println(err)
			})
			return nil
		},
	)

	log.Println("Running echo server")
	// Serve the outgoing directory
	ctx.BindStartupHandle(context.Background())
}
