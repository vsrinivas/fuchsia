// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	"context"
	"sync"
	"syscall/zx/fidl"
	"testing"

	"fidl/fuchsia/power"

	"go.fuchsia.dev/fuchsia/src/lib/component"
)

type WatcherMock struct {
	called chan struct{}
}

func (pmw *WatcherMock) OnChangeBatteryInfo(fidl.Context, power.BatteryInfo) error {
	select {
	case pmw.called <- struct{}{}:
	default:
	}
	return nil
}

var ctx = component.NewContextFromStartupInfo()

func TestPowerManager(t *testing.T) {
	req, iface, err := power.NewBatteryManagerWithCtxInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		_ = iface.Close()
	}()
	ctx.ConnectToEnvService(req)
	if _, err := iface.GetBatteryInfo(context.Background()); err != nil {
		t.Fatal(err)
	}
}

func TestBatteryInfoWatcher(t *testing.T) {
	r, p, err := power.NewBatteryManagerWithCtxInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		_ = p.Close()
	}()
	ctx.ConnectToEnvService(r)

	var wg sync.WaitGroup
	defer wg.Wait()

	rw, pw, err := power.NewBatteryInfoWatcherWithCtxInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		_ = rw.Close()
	}()
	if err := p.Watch(context.Background(), *pw); err != nil {
		t.Fatal(err)
	}

	pmWatcher := WatcherMock{called: make(chan struct{})}

	wg.Add(1)
	go func() {
		defer wg.Done()

		component.ServeExclusive(context.Background(), &power.BatteryInfoWatcherWithCtxStub{
			Impl: &pmWatcher,
		}, rw.Channel, func(err error) { t.Log(err) })
	}()

	<-pmWatcher.called
}
