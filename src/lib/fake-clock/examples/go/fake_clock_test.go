// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

// This is an example of a Go use of FakeClock to show that the syscalls are
// being overwritten. This file is based on the C++ test bed located in
// //src/lib/fake-clock/lib/fake_clock_test.cc and include a subset of those
// tests.

package fake_clock

import (
	"context"
	"syscall/zx"
	"testing"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	mockclock "fidl/fuchsia/testing"
)

type FakeClockTest struct {
	control *mockclock.FakeClockControlWithCtxInterface
}

// Helper for setting up the test, returns a control handle for ease of
// interacting with the fake clock. Also pauses so that the fake clock always
// starts in the same state.
func setup(t *testing.T) FakeClockTest {
	service, control, err := mockclock.NewFakeClockControlWithCtxInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}
	component.NewContextFromStartupInfo().ConnectToEnvService(service)

	// Always pause the fake clock before the test starts.
	if err := control.Pause(context.Background()); err != nil {
		t.Fatal(err)
	}

	t.Cleanup(func() {
		if err := control.Close(); err != nil {
			t.Error(err)
		}
	})

	return FakeClockTest{
		control: control,
	}
}

// Utility for advancing the fake clock by an integer amount.
func (f *FakeClockTest) advance(i int64) {
	f.control.Advance(context.Background(), mockclock.IncrementWithDetermined(i))
}

// Utility for calling the syscall for time which is intercepted by the fake
// clock.
func getSysTime() zx.Time {
	return zx.Sys_clock_get_monotonic()
}

// Advance the time by a constant, and ensure that time only advances by that
// constant.
func TestTimeAdvance(t *testing.T) {
	t.Skip("TODO(https://fxbug.dev/75987): figure out zx_futex_wait flake")
	control := setup(t)

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
	t.Skip("TODO(https://fxbug.dev/75987): figure out zx_futex_wait flake")
	setup(t)

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
	t.Skip("TODO(https://fxbug.dev/75987): figure out zx_futex_wait flake")
	control := setup(t)
	ch := make(chan struct{})

	var dur zx.Duration = 500
	deadline := zx.Sys_deadline_after(dur)
	go func() {
		zx.Sys_nanosleep(deadline)
		close(ch)
	}()

	control.advance(250)
	select {
	case <-ch:
		t.Fatal("Nanosleep terminated before expected 500 ns.")
	default:
		// Expected case.
	}

	control.advance(250)
	// Waits for the goroutine to finish.
	<-ch
}

// Tests that timing out on a Wait works as expected and status/signals are
// correct.
func TestWaitOneTimeout(t *testing.T) {
	t.Skip("TODO(https://fxbug.dev/75987): figure out zx_futex_wait flake")
	control := setup(t)
	ch := make(chan zx.Status)

	var dur zx.Duration = 500
	deadline := zx.Sys_deadline_after(dur)

	event, err := zx.NewEvent(0)
	if err != nil {
		t.Fatalf("zx.NewEvent failed with err = %s", err)
	}
	defer func() {
		if err := event.Close(); err != nil {
			t.Error(err)
		}
	}()

	var obs zx.Signals
	go func() {
		ch <- zx.Sys_object_wait_one(*event.Handle(), zx.SignalEventSignaled, deadline, &obs)
	}()

	control.advance(500)

	if got, want := <-ch, zx.ErrTimedOut; got != want {
		t.Fatalf("got status = %s, want %s", got, want)
	}

	if obs != zx.SignalNone {
		t.Fatalf("Got signal = %#v, want signal = %#v", obs, zx.SignalNone)
	}
}

// Tests that signaling on a Wait works as expected and status/signals are
// correct.
func TestWaitOneSignal(t *testing.T) {
	t.Skip("TODO(https://fxbug.dev/75987): figure out zx_futex_wait flake")
	setup(t)
	ch := make(chan zx.Status)

	var dur zx.Duration = 500
	deadline := zx.Sys_deadline_after(dur)

	event, err := zx.NewEvent(0)
	if err != nil {
		t.Fatalf("zx.NewEvent failed with err = %s", err)
	}
	defer func() {
		if err := event.Close(); err != nil {
			t.Error(err)
		}
	}()

	var obs zx.Signals
	go func() {
		ch <- zx.Sys_object_wait_one(*event.Handle(), zx.SignalEventSignaled, deadline, &obs)
	}()

	if err := event.Handle().Signal(0, zx.SignalEventSignaled); err != nil {
		t.Fatal(err)
	}

	if got, want := <-ch, zx.ErrOk; got != want {
		t.Fatalf("got status = %s, want %s", got, want)
	}
	if obs != zx.SignalEventSignaled {
		t.Fatalf("Got signal = %#v, want signal = %#v", obs, zx.SignalEventSignaled)
	}
}

// Tests that zx.WaitMany times out as expected with just 2 WaitItems.
func TestWaitManyTimeoutSmall(t *testing.T) {
	t.Skip("TODO(https://fxbug.dev/75987): figure out zx_futex_wait flake")
	control := setup(t)
	ch := make(chan error)

	var dur zx.Duration = 500
	deadline := zx.Sys_deadline_after(dur)

	event1, err := zx.NewEvent(0)
	if err != nil {
		t.Fatalf("zx.NewEvent failed with err = %s", err)
	}
	defer func() {
		if err := event1.Close(); err != nil {
			t.Error(err)
		}
	}()
	event2, err := zx.NewEvent(0)
	if err != nil {
		t.Fatalf("zx.NewEvent failed with err = %s", err)
	}
	defer func() {
		if err := event2.Close(); err != nil {
			t.Error(err)
		}
	}()

	items := []zx.WaitItem{
		{*event1.Handle(), zx.SignalEventSignaled, 0},
		{*event2.Handle(), zx.SignalEventSignaled, 0},
	}

	go func() {
		ch <- zx.WaitMany(items, deadline)
	}()

	control.advance(500)

	switch err := (<-ch).(type) {
	case *zx.Error:
		if err.Status != zx.ErrTimedOut {
			t.Fatalf("Got status = %s, want status = %s", err.Status, zx.ErrTimedOut)
		}
	default:
		t.Fatalf("Unexpected error type: %T", err)
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
	t.Skip("TODO(https://fxbug.dev/75987): figure out zx_futex_wait flake")
	setup(t)
	ch := make(chan error)

	var dur zx.Duration = 500
	deadline := zx.Sys_deadline_after(dur)

	event1, err := zx.NewEvent(0)
	if err != nil {
		t.Fatalf("zx.NewEvent failed with err = %s", err)
	}
	defer func() {
		if err := event1.Close(); err != nil {
			t.Error(err)
		}
	}()
	event2, err := zx.NewEvent(0)
	if err != nil {
		t.Fatalf("zx.NewEvent failed with err = %s", err)
	}
	defer func() {
		if err := event2.Close(); err != nil {
			t.Error(err)
		}
	}()

	items := []zx.WaitItem{
		{*event1.Handle(), zx.SignalEventSignaled, 0},
		{*event2.Handle(), zx.SignalEventSignaled, 0},
	}

	go func() {
		ch <- zx.WaitMany(items, deadline)
	}()

	if err := event1.Handle().Signal(0, zx.SignalEventSignaled); err != nil {
		t.Fatal(err)
	}

	if err := <-ch; err != nil {
		t.Fatalf("Got: err = %s, want nil", err)
	}
	if obs := items[0].Pending; obs != zx.SignalEventSignaled {
		t.Fatalf("Got signal = %#v, want signal = %#v", obs, zx.SignalEventSignaled)
	}
	if obs := items[1].Pending; obs != zx.SignalNone {
		t.Fatalf("Got signal = %#v, want signal = %#v", obs, zx.SignalNone)
	}
}
