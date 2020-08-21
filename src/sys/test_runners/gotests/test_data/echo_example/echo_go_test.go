// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package echo_go_test

import (
	"context"
	"fidl/fidl/examples/routing/echo"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	"testing"
)

func TestEcho(t *testing.T) {
	ctx := component.NewContextFromStartupInfo()
	echoReq, echoInterface, err := echo.NewEchoWithCtxInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		_ = echoInterface.Close()
	}()
	ctx.ConnectToEnvService(echoReq)
	msg := "test message"
	response, err := echoInterface.EchoString(context.Background(), &msg)
	if err != nil {
		t.Fatal(err)
	}
	if *response != msg {
		t.Errorf("got: %s; want: %s", *response, msg)
	}
}
