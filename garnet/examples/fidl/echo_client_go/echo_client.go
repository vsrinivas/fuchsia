// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/io"

	"app/context"

	"fidl/fidl/examples/echo"
	"fidl/fuchsia/sys"
)

func main() {
	serverURL := flag.String("server", "fuchsia-pkg://fuchsia.com/echo_server_go#meta/echo_server_go.cmx", "server URL")
	msg := flag.String("m", "Hello, Go World", "message")

	flag.Parse()

	ctx := context.CreateFromStartupInfo()

	directoryReq, directoryInterface, err := io.NewDirectoryInterfaceRequest()
	if err != nil {
		panic(err)
	}
	launchInfo := sys.LaunchInfo{
		Url:              *serverURL,
		DirectoryRequest: directoryReq.Channel,
	}

	componentControllerReq, _, err := sys.NewComponentControllerInterfaceRequest()
	if err != nil {
		panic(err)
	}
	if err := ctx.Launcher().CreateComponent(launchInfo, componentControllerReq); err != nil {
		panic(err)
	}

	echoReq, echoInterface, err := echo.NewEchoInterfaceRequest()
	if err != nil {
		panic(err)
	}
	if err := fdio.ServiceConnectAt(zx.Handle(directoryInterface.Channel), echoReq.Name(), zx.Handle(echoReq.Channel)); err != nil {
		panic(err)
	}

	response, err := echoInterface.EchoString(msg)
	if err != nil {
		panic(err)
	}
	fmt.Println("client:", *response)
}
