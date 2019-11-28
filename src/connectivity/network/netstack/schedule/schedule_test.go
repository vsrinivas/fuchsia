// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package schedule_test

import (
	"context"
	"runtime"
	"testing"
	"time"

	"netstack/schedule"
)

func TestOncePerTick(t *testing.T) {
	type testCase struct {
		ops                          []func()
		name                         string
		oncePerTickInvocationsWanted int
		onNotifyInvocationsWanted    int
	}

	notify := make(chan struct{})
	rate := make(chan time.Time)
	sendRate := func() {
		rate <- time.Time{}
	}
	sendNotify := func() {
		notify <- struct{}{}
	}
	interleavings := []testCase{
		{
			name:                         "no_ticks",
			ops:                          nil,
			oncePerTickInvocationsWanted: 0,
			onNotifyInvocationsWanted:    0,
		},
		{
			name:                         "rate_ticks_only",
			ops:                          []func(){sendRate, sendRate},
			oncePerTickInvocationsWanted: 2,
			onNotifyInvocationsWanted:    0,
		},
		{
			name:                         "notification_then_rate_tick",
			ops:                          []func(){sendNotify, sendRate},
			oncePerTickInvocationsWanted: 1,
			onNotifyInvocationsWanted:    1,
		},
		{
			name:                         "rate_tick_then_notification",
			ops:                          []func(){sendRate, sendNotify},
			oncePerTickInvocationsWanted: 2,
			onNotifyInvocationsWanted:    1,
		},
		{
			name:                         "notification_between_rate_ticks",
			ops:                          []func(){sendRate, sendNotify, sendRate},
			oncePerTickInvocationsWanted: 2,
			onNotifyInvocationsWanted:    1,
		},
		{
			name:                         "multiple_notifications_per_rate_tick",
			ops:                          []func(){sendNotify, sendNotify, sendRate},
			oncePerTickInvocationsWanted: 1,
			onNotifyInvocationsWanted:    2,
		},
	}

	for _, interleaving := range interleavings {
		t.Run(interleaving.name, func(t *testing.T) {
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			onNotifyInvocations := 0
			onNotify := func() { onNotifyInvocations++ }
			oncePerTickInvocations := 0
			oncePerTick := func(time.Time) { oncePerTickInvocations++ }
			errors := make(chan error)
			go func() {
				errors <- schedule.OncePerTick(ctx, notify, rate, onNotify, oncePerTick)
			}()

			for _, op := range interleaving.ops {
				op()

				// Yield so the goroutine we spawned has a chance to run.
				runtime.Gosched()

				select {
				case err := <-errors:
					t.Fatalf("expected no errors, got: %s", err)
				default:
				}
			}

			cancel()
			<-errors

			if got, want := oncePerTickInvocations, interleaving.oncePerTickInvocationsWanted; got != want {
				t.Fatalf("got %d invocations of oncePerTick, want %d", got, want)
			}
			if got, want := onNotifyInvocations, interleaving.onNotifyInvocationsWanted; got != want {
				t.Fatalf("got %d notifications of onNotify, want %d", got, want)
			}
		})
	}

	t.Run("context.Done", func(t *testing.T) {
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		notify := make(chan struct{})
		rate := make(chan time.Time)
		errors := make(chan error)
		var got int
		go func() {
			errors <- schedule.OncePerTick(ctx, notify, rate, func() {}, func(time.Time) { got++ })
		}()

		cancel()

		select {
		case notify <- struct{}{}:
			t.Fatal("send on notify ch should fail")
		default:
		}

		if want := 0; got != want {
			t.Errorf("got %d notifications on channel, want %d", got, want)
		}

		if <-errors == nil {
			t.Fatal("got nil error, want non-nil")
		}
	})

	t.Run("timestamp", func(t *testing.T) {
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		notify := make(chan struct{})
		rate := make(chan time.Time)
		want := time.Unix(1, 0)
		var got time.Time
		recordTimestamp := func(ts time.Time) { got = ts }
		errors := make(chan error)
		go func() {
			errors <- schedule.OncePerTick(ctx, notify, rate, func() {}, recordTimestamp)
		}()
		rate <- want

		select {
		case err := <-errors:
			t.Fatalf("got <-errors = %s, want no channel send", err)
		default:
		}

		if got != want {
			t.Fatalf("got recordPeriod(%+v), want recordPeriod(%+v)", got, want)
		}
	})
}
