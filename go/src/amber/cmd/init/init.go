// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"application/lib/app/context"
	"fidl/bindings"
	"garnet/amber/api/amber"
)

func connect(ctx *context.Context) (*amber.Control_Proxy, amber.Control_Request) {
	var pxy *amber.Control_Proxy
	req, pxy := pxy.NewRequest(bindings.GetAsyncWaiter())
	ctx.ConnectToEnvService(req)
	return pxy, req
}

func main() {
	proxy, _ := connect(context.CreateFromStartupInfo())
	proxy.Close()
}
