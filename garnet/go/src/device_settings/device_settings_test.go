// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"syscall/zx/fidl"
	"testing"

	"app/context"
	"fidl/fuchsia/devicesettings"
)

const (
	TestSettingKey = "TestSetting"
)

func TestDeviceSettingsSimple(t *testing.T) {
	ctx := context.CreateFromStartupInfo()
	req, dm, err := devicesettings.NewDeviceSettingsManagerWithCtxInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}
	ctx.ConnectToEnvService(req)

	if s, err := dm.SetInteger(fidl.Background(), TestSettingKey, 10); err != nil {
		t.Fatal(err)
	} else if !s {
		t.Fatalf("Set Integer failed")
	}
	if i, s, err := dm.GetInteger(fidl.Background(), TestSettingKey); err != nil {
		t.Fatal(err)
	} else if s != devicesettings.StatusOk {
		t.Fatalf("got err status: %s", s)
	} else if i != 10 {
		t.Fatalf("expected 10 got : %d", i)
	}

	if s, err := dm.SetString(fidl.Background(), TestSettingKey, "somestring"); err != nil {
		t.Fatal(err)
	} else if !s {
		t.Fatalf("Set String failed")
	}
	if str, s, err := dm.GetString(fidl.Background(), TestSettingKey); err != nil {
		t.Fatal(err)
	} else if s != devicesettings.StatusOk {
		t.Fatalf("got err status: %s", s)
	} else if str != "somestring" {
		t.Fatalf("expected 'somestring' got : %q", str)
	}
}
