// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is an example of a Go use of FakeClock to show that the syscalls are
// being overwritten. This file is based on the C++ test bed located in
// //src/lib/fake-clock/lib/fake_clock_test.cc and include a subset of those
// tests.

package fake_clock

import (
	"context"
	"runtime"
	"syscall/zx"
	"syscall/zx/zxwait"
	"testing"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	mock_clock "fidl/fuchsia/testing"
)

type FakeClockTest struct {
	control *mock_clock.FakeClockControlWithCtxInterface
}

// Helper for setting up the test, returns a control handle for ease of
// interacting with the fake clock. Also pauses so that the fake clock always
// starts in the same state.
func setup(t *testing.T) FakeClockTest {
	service, control, _ := mock_clock.NewFakeClockControlWithCtxInterfaceRequest()
	component.NewContextFromStartupInfo().ConnectToEnvService(service)

	// Always pause the fake clock before the test starts.
	if err := control.Pause(context.Background()); err != nil {
		t.Fatalf("Error pausing: %s", err)
	}

	return FakeClockTest{control}
}

func (f *FakeClockTest) Close() error {
	return f.control.Close()
}

// Utility for advancing the fake clock by an integer amount.
func (f *FakeClockTest) advance(i int64) {
	f.control.Advance(context.Background(), mock_clock.IncrementWithDetermined(i))
}

// Utility for calling the syscall for time which is intercepted by the fake
// clock.
func getSysTime() zx.Time {
	return zx.Sys_clock_get_monotonic()
}

// Advance the time by a constant, and ensure that time only advances by that
// constant.
func TestTimeAdvance(t *testing.T) {
	// TODO(fxbug.dev/45644): Disabled due to flake
	t.Skip("Skipped: zx_futex_wait flake (fxbug.dev/45644)")
	control := setup(t)
	defer control.Close()

	t1 := getSysTime()
	control.advance(1)
	t2 := getSysTime()

	if t2 != t1+1 {
		t.Fatalf("Got t1 = %d, t2 = %d, want t2 = %d", t1, t2, t1+1)
	}
}

// Test the deadline_after syscall, ensure that the time returned is the paused
// time plus the duration.
func TestDeadlineAfter(t *testing.T) {
	// TODO(fxbug.dev/45644): Disabled due to flake
	t.Skip("Skipped: zx_futex_wait flake (fxbug.dev/45644)")
	control := setup(t)
	defer control.Close()

	var dur zx.Duration = 50

	t1 := getSysTime()
	t2 := zx.Sys_deadline_after(dur)

	if t2 != t1+50 {
		t.Fatalf("Got t1 = %d, t2 = %d, want t2 = %d", t1, t2, t1+50)
	}
}

// Tests that the nanosleep blocking syscall wakes up after advancing the fake
// clock by the expected number of ns.
func TestNanosleep(t *testing.T) {
	// TODO(fxbug.dev/45644): Disabled due to flake
	t.Skip("Skipped: zx_futex_wait flake (fxbug.dev/45644)")
	control := setup(t)
	defer control.Close()
	done := make(chan bool)

	var dur zx.Duration = 500
	deadline := zx.Sys_deadline_after(dur)
	go func() {
		zx.Sys_nanosleep(deadline)
		done <- true
	}()

	control.advance(250)
	select {
	case <-done:
		t.Fatal("Nanosleep terminated before expected 500 ns.")
	default:
		// Expected case.
	}

	control.advance(250)
	// Waits for the goroutine to finish.
	<-done
}

// Ensures that the output of Sys_clock_get(ZX_CLOCK_MONOTONIC) matches the
// output of Sys_clock_get_monotonic.
func TestClockGet(t *testing.T) {
	// TODO(fxbug.dev/45644): Disabled due to flake
	t.Skip("Skipped: zx_futex_wait flake (fxbug.dev/45644)")
	control := setup(t)
	defer control.Close()

	control.advance(90000)
	t1 := getSysTime()
	var t2 zx.Time
	if status := zx.Sys_clock_get(runtime.ZX_CLOCK_MONOTONIC, &t2); status != zx.ErrOk {
		t.Fatalf("Sys_clock_get(ZX_CLOCK_MONOTONIC, %d) returned error: %s", t2, status)
	}
	if t1 != t2 {
		t.Fatalf("clock_get_monotonic returned: %d, clock_get(ZX_CLOCK_MONOTONIC) returned: %d", t1, t2)
	}
}

// Tests that timing out on a Wait works as expected and status/signals are
// correct.
func TestWaitOneTimeout(t *testing.T) {
	// TODO(fxbug.dev/45644): Disabled due to flake
	t.Skip("Skipped: zx_futex_wait flake (fxbug.dev/45644)")
	control := setup(t)
	defer control.Close()
	done := make(chan bool)

	var dur zx.Duration = 500
	deadline := zx.Sys_deadline_after(dur)

	event, err := zx.NewEvent(0)
	if err != nil {
		t.Fatalf("zx.NewEvent failed with err = %s", err)
	}
	defer event.Close()

	var obs zx.Signals
	var error error
	go func() {
		obs, error = zxwait.Wait(*event.Handle(), zx.SignalEventSignaled, deadline)
		done <- true
	}()

	control.advance(500)
	<-done

	if error == nil {
		t.Fatal("Got: nil, want: zx.ErrTimedOut")
	}
	switch error := error.(type) {
	case *zx.Error:
		if error.Status != zx.ErrTimedOut {
			t.Fatalf("Got status = %s, want status = %s", error.Status, zx.ErrTimedOut)
		}
	default:
		t.Fatalf("Unexpected error type: %T", error)
	}

	if obs != zx.SignalNone {
		t.Fatalf("Got signal = %#v, want signal = %#v", obs, zx.SignalNone)
	}
}

// Tests that signaling on a Wait works as expected and status/signals are
// correct.
func TestWaitOneSignal(t *testing.T) {
	// TODO(fxbug.dev/45644): Disabled due to flake
	t.Skip("Skipped: zx_futex_wait flake (fxbug.dev/45644)")
	control := setup(t)
	defer control.Close()
	done := make(chan bool)

	var dur zx.Duration = 500
	deadline := zx.Sys_deadline_after(dur)

	event, err := zx.NewEvent(0)
	if err != nil {
		t.Fatalf("zx.NewEvent failed with err = %s", err)
	}
	defer event.Close()

	var obs zx.Signals
	var error error
	go func() {
		obs, error = zxwait.Wait(*event.Handle(), zx.SignalEventSignaled, deadline)
		done <- true
	}()

	event.Handle().Signal(0, zx.SignalEventSignaled)
	<-done

	if error != nil {
		t.Fatalf("Got: err = %s, want nil", error)
	}
	if obs != zx.SignalEventSignaled {
		t.Fatalf("Got signal = %#v, want signal = %#v", obs, zx.SignalEventSignaled)
	}
}

// Tests that zx.WaitMany times out as expected with just 2 WaitItems.
func TestWaitManyTimeoutSmall(t *testing.T) {
	// TODO(fxbug.dev/45644): Disabled due to flake
	t.Skip("Skipped: zx_futex_wait flake (fxbug.dev/45644)")
	control := setup(t)
	defer control.Close()
	done := make(chan bool)

	var dur zx.Duration = 500
	deadline := zx.Sys_deadline_after(dur)

	event1, err := zx.NewEvent(0)
	if err != nil {
		t.Fatalf("zx.NewEvent failed with err = %s", err)
	}
	defer event1.Close()
	event2, err := zx.NewEvent(0)
	if err != nil {
		t.Fatalf("zx.NewEvent failed with err = %s", err)
	}
	defer event2.Close()

	items := []zx.WaitItem{
		{*event1.Handle(), zx.SignalEventSignaled, 0},
		{*event2.Handle(), zx.SignalEventSignaled, 0},
	}

	var error error
	go func() {
		error = zx.WaitMany(items, deadline)
		done <- true
	}()

	control.advance(500)
	<-done

	if error == nil {
		t.Fatal("Got: nil, want: zx.ErrTimedOut")
	}
	switch error := error.(type) {
	case *zx.Error:
		if error.Status != zx.ErrTimedOut {
			t.Fatalf("Got status = %s, want status = %s", error.Status, zx.ErrTimedOut)
		}
	default:
		t.Fatalf("Unexpected error type: %T", error)
	}

	if obs := items[0].Pending; obs != zx.SignalNone {
		t.Fatalf("Got signal = %#v, want signal = %#v", obs, zx.SignalNone)
	}
	if obs := items[1].Pending; obs != zx.SignalNone {
		t.Fatalf("Got signal = %#v, want signal = %#v", obs, zx.SignalNone)
	}
}

// Tests that zx.WaitMany signals as expected with just 2 WaitItems.
func TestWaitManySignalSmall(t *testing.T) {
	// TODO(fxbug.dev/45644): Disabled due to flake
	t.Skip("Skipped: zx_futex_wait flake (fxbug.dev/45644)")
	control := setup(t)
	defer control.Close()
	done := make(chan bool)

	var dur zx.Duration = 500
	deadline := zx.Sys_deadline_after(dur)

	event1, err := zx.NewEvent(0)
	if err != nil {
		t.Fatalf("zx.NewEvent failed with err = %s", err)
	}
	defer event1.Close()
	event2, err := zx.NewEvent(0)
	if err != nil {
		t.Fatalf("zx.NewEvent failed with err = %s", err)
	}
	defer event2.Close()

	items := []zx.WaitItem{
		{*event1.Handle(), zx.SignalEventSignaled, 0},
		{*event2.Handle(), zx.SignalEventSignaled, 0},
	}

	var error error
	go func() {
		error = zx.WaitMany(items, deadline)
		done <- true
	}()

	event1.Handle().Signal(0, zx.SignalEventSignaled)
	<-done

	if error != nil {
		t.Fatalf("Got: err = %s, want nil", error)
	}
	if obs := items[0].Pending; obs != zx.SignalEventSignaled {
		t.Fatalf("Got signal = %#v, want signal = %#v", obs, zx.SignalEventSignaled)
	}
	if obs := items[1].Pending; obs != zx.SignalNone {
		t.Fatalf("Got signal = %#v, want signal = %#v", obs, zx.SignalNone)
	}
}
