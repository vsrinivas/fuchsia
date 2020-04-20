// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"sync/atomic"
	"testing"
	"time"

	appcontext "app/context"
	"syscall/zx/fidl"

	"fidl/fuchsia/power"
)

type ClientMock struct {
	pm *power.BatteryManagerWithCtxInterface
}

type WatcherMock struct {
	called uint32
}

func (pmw *WatcherMock) OnChangeBatteryInfo(_ fidl.Context, bi power.BatteryInfo) error {
	atomic.AddUint32(&pmw.called, 1)
	return nil
}

func TestPowerManager(t *testing.T) {
	ctx := appcontext.CreateFromStartupInfo()
	req, iface, err := power.NewBatteryManagerWithCtxInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}
	pmClient := &ClientMock{}
	pmClient.pm = iface
	ctx.ConnectToEnvService(req)
	_, err = pmClient.pm.GetBatteryInfo(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	pmClient.pm.Close()
}

func TestBatteryInfoWatcher(t *testing.T) {
	ctx := appcontext.CreateFromStartupInfo()
	r, p, err := power.NewBatteryManagerWithCtxInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}
	pmClient := &ClientMock{}
	pmClient.pm = p
	ctx.ConnectToEnvService(r)

	rw, pw, err := power.NewBatteryInfoWatcherWithCtxInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}

	pmWatcher := &WatcherMock{called: 0}
	s := power.BatteryInfoWatcherWithCtxStub{Impl: pmWatcher}
	bi := fidl.BindingSet{}
	bi.Add(&s, rw.Channel, nil)
	go fidl.Serve()

	err = pmClient.pm.Watch(context.Background(), *pw)
	if err != nil {
		t.Fatal(err)
	}

	timeToWait := 5000 // in ms
	timeToSleep := 500 // in ms
	val := uint32(0)
	for i := 0; i < timeToWait/timeToSleep; i++ {
		val = atomic.LoadUint32(&pmWatcher.called)
		if val == 0 {
			time.Sleep(time.Duration(timeToSleep) * time.Millisecond)
		} else {
			break
		}
	}

	if val == 0 {
		t.Fatalf("Watcher should have been called by now")
	}
}
