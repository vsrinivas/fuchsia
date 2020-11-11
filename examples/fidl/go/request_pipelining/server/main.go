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

type echoImpl struct {
	prefix string
}

// This implementation of Echo responds with a prefix prepended to every response.
func (echo *echoImpl) EchoString(_ fidl.Context, inValue string) (string, error) {
	log.Println("Received EchoString request:", inValue)
	return echo.prefix + inValue, nil
}

// The SendString method is not used in this example, so just do nothing.
func (echo *echoImpl) SendString(_ fidl.Context, inValue string) error {
	return nil
}

type echoLauncherImpl struct{}

// Non pipelined method for obtaining an Echo
func (launcher *echoLauncherImpl) GetEcho(ctx fidl.Context, prefix string) (examples.EchoWithCtxInterface, error) {
	// In the non-pipelined case, the server is responsible for initializing the channel. It
	// then binds an Echo implementation to the server end, and responds with
	// the client end
	serverEnd, clientEnd, err := examples.NewEchoWithCtxInterfaceRequest()
	if err != nil {
		return examples.EchoWithCtxInterface{}, err
	}

	stub := examples.EchoWithCtxStub{Impl: &echoImpl{prefix: prefix}}
	go component.ServeExclusive(ctx, &stub, serverEnd.ToChannel(), func(err error) {
		log.Println(err)
	})

	return *clientEnd, nil
}

// Pipelined method for obtaining an Echo
func (launcher *echoLauncherImpl) GetEchoPipelined(ctx fidl.Context, prefix string, serverEnd examples.EchoWithCtxInterfaceRequest) error {
	// In the pipelined case, the client is responsible for initializing the
	// channel. It keeps the client end and passes the server end in the request.
	stub := examples.EchoWithCtxStub{Impl: &echoImpl{prefix: prefix}}
	go component.ServeExclusive(ctx, &stub, serverEnd.ToChannel(), func(err error) {
		log.Println(err)
	})
	return nil
}

func main() {
	log.SetFlags(log.Lshortfile)

	ctx := component.NewContextFromStartupInfo()

	ctx.OutgoingService.AddService(
		examples.EchoLauncherName,
		func(ctx fidl.Context, c zx.Channel) error {
			stub := examples.EchoLauncherWithCtxStub{Impl: &echoLauncherImpl{}}
			go component.ServeExclusive(ctx, &stub, c, func(err error) {
				log.Println(err)
			})
			return nil
		},
	)

	log.Println("Running echo launcher server")
	ctx.BindStartupHandle(context.Background())
}
