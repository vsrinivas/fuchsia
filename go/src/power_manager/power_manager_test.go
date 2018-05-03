// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"sync/atomic"
	"testing"
	"time"

	"app/context"
	"fidl/bindings"

	"fidl/power_manager"
)

type ClientMock struct {
	pm *power_manager.PowerManagerInterface
}

type WatcherMock struct {
	called uint32
}

func (pmw *WatcherMock) OnChangeBatteryStatus(bs power_manager.BatteryStatus) error {
	atomic.AddUint32(&pmw.called, 1)
	return nil
}

func TestPowerManager(t *testing.T) {
	ctx := context.CreateFromStartupInfo()
	req, iface, err := power_manager.NewPowerManagerInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}
	pmClient := &ClientMock{}
	pmClient.pm = iface
	ctx.ConnectToEnvService(req)
	_, err = pmClient.pm.GetBatteryStatus()
	if err != nil {
		t.Fatal(err)
	}
	pmClient.pm.Close()
}

func TestPowerManagerWatcher(t *testing.T) {
	ctx := context.CreateFromStartupInfo()
	r, p, err := power_manager.NewPowerManagerInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}
	pmClient := &ClientMock{}
	pmClient.pm = p
	ctx.ConnectToEnvService(r)

	rw, pw, err := power_manager.NewPowerManagerWatcherInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}

	pmWatcher := &WatcherMock{called: 0}
	s := power_manager.PowerManagerWatcherStub{Impl: pmWatcher}
	bs := bindings.BindingSet{}
	bs.Add(&s, rw.Channel, nil)
	go bindings.Serve()

	err = pmClient.pm.Watch(*pw)
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
