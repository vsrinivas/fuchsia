// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"sync/atomic"
	"syscall/mx"
	"syscall/mx/mxerror"
	"testing"
	"time"

	"app/context"
	"fidl/bindings"

	"garnet/public/lib/power/fidl/power_manager"
)

type ClientMock struct {
	pm *power_manager.PowerManager_Proxy
}

type WatcherMock struct {
	called uint32
	pmw    *power_manager.PowerManagerWatcher_Proxy
}

func (pmw *WatcherMock) OnChangeBatteryStatus(bs power_manager.BatteryStatus) error {
	atomic.AddUint32(&pmw.called, 1)
	return nil
}

func TestPowerManager(t *testing.T) {
	ctx := context.CreateFromStartupInfo()
	pmClient := &ClientMock{}
	r, p := pmClient.pm.NewRequest(bindings.GetAsyncWaiter())
	pmClient.pm = p
	ctx.ConnectToEnvService(r)
	_, err := pmClient.pm.GetBatteryStatus()
	if err != nil {
		t.Fatal(err)
	}
	pmClient.pm.Close()
}

func TestPowerManagerWatcher(t *testing.T) {
	ctx := context.CreateFromStartupInfo()
	pmClient := &ClientMock{}
	r, p := pmClient.pm.NewRequest(bindings.GetAsyncWaiter())
	pmClient.pm = p
	ctx.ConnectToEnvService(r)

	pmWatcher := &WatcherMock{called: 0}
	rw, pw := power_manager.NewChannelForPowerManagerWatcher()
	s := rw.NewStub(pmWatcher, bindings.GetAsyncWaiter())

	go func() {
		for {
			if err := s.ServeRequest(); err != nil {
				if mxerror.Status(err) != mx.ErrPeerClosed {
					log.Println(err)
				}
				break
			}
		}
	}()

	err := pmClient.pm.Watch(pw)
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
