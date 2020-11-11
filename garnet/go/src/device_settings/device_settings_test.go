// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	"context"
	"testing"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fuchsia/devicesettings"
)

const (
	TestSettingKey = "TestSetting"
)

func TestDeviceSettingsSimple(t *testing.T) {
	ctx := component.NewContextFromStartupInfo()
	req, dm, err := devicesettings.NewDeviceSettingsManagerWithCtxInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}
	ctx.ConnectToEnvService(req)

	if s, err := dm.SetInteger(context.Background(), TestSettingKey, 10); err != nil {
		t.Fatal(err)
	} else if !s {
		t.Fatalf("Set Integer failed")
	}
	if i, s, err := dm.GetInteger(context.Background(), TestSettingKey); err != nil {
		t.Fatal(err)
	} else if s != devicesettings.StatusOk {
		t.Fatalf("got err status: %s", s)
	} else if i != 10 {
		t.Fatalf("expected 10 got : %d", i)
	}

	if s, err := dm.SetString(context.Background(), TestSettingKey, "somestring"); err != nil {
		t.Fatal(err)
	} else if !s {
		t.Fatalf("Set String failed")
	}
	if str, s, err := dm.GetString(context.Background(), TestSettingKey); err != nil {
		t.Fatal(err)
	} else if s != devicesettings.StatusOk {
		t.Fatalf("got err status: %s", s)
	} else if str != "somestring" {
		t.Fatalf("expected 'somestring' got : %q", str)
	}
}
