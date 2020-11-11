// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	"context"
	"flag"
	"fmt"
	"syscall/zx"
	"syscall/zx/fdio"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fidl/examples/echo"
	"fidl/fuchsia/io"
	"fidl/fuchsia/sys"
)

func main() {
	serverURL := flag.String("server", "fuchsia-pkg://fuchsia.com/echo_server_go#meta/echo_server_go.cmx", "server URL")
	msg := flag.String("m", "Hello, Go World", "message")

	flag.Parse()

	ctx := component.NewContextFromStartupInfo()

	directoryReq, directoryInterface, err := io.NewDirectoryWithCtxInterfaceRequest()
	if err != nil {
		panic(err)
	}
	launchInfo := sys.LaunchInfo{
		Url:              *serverURL,
		DirectoryRequest: directoryReq.Channel,
	}

	componentControllerReq, _, err := sys.NewComponentControllerWithCtxInterfaceRequest()
	if err != nil {
		panic(err)
	}
	if err := ctx.Launcher().CreateComponent(context.Background(), launchInfo, componentControllerReq); err != nil {
		panic(err)
	}

	echoReq, echoInterface, err := echo.NewEchoWithCtxInterfaceRequest()
	if err != nil {
		panic(err)
	}
	if err := fdio.ServiceConnectAt(zx.Handle(directoryInterface.Channel), echoReq.Name(), zx.Handle(echoReq.Channel)); err != nil {
		panic(err)
	}

	response, err := echoInterface.EchoString(context.Background(), msg)
	if err != nil {
		panic(err)
	}
	fmt.Println("client:", *response)
}
