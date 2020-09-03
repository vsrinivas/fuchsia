// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package concurrency_test

import (
	"context"
	"fidl/fidl/examples/routing/echo"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	"strconv"
	"testing"
)

func test_helper(t *testing.T, count int) {
	ctx := component.NewContextFromStartupInfo()
	echoReq, echoInterface, err := echo.NewEchoWithCtxInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		_ = echoInterface.Close()
	}()
	ctx.ConnectToEnvService(echoReq)
	msg := "test message" + strconv.Itoa(count)
	response, err := echoInterface.EchoString(context.Background(), &msg)
	if err != nil {
		t.Fatal(err)
	}
	if *response != msg {
		t.Errorf("got: %s; want: %s", *response, msg)
	}
}

func Test1(t *testing.T) {
	test_helper(t, 1)
}

func Test2(t *testing.T) {
	test_helper(t, 2)
}

func Test3(t *testing.T) {
	test_helper(t, 3)
}

func Test4(t *testing.T) {
	test_helper(t, 4)
}

func Test5(t *testing.T) {
	test_helper(t, 5)
}
