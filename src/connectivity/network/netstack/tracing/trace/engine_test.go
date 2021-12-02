// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build tracing
// +build tracing

package trace

import (
	"testing"
	"time"
)

// Test if the engine can start and stop correctly while the trace context
// is acquired and released by multipel goroutines.
func TestAcquireContextConcurrent(t *testing.T) {
	quit := make(chan struct{})

	// N goroutines keep calling acquireContext and releaseContext.
	const n = 10
	done := make(chan struct{}, n)
	for i := 0; i < n; i++ {
		go func() {
		loop:
			for {
				select {
				case <-quit:
					break loop
				default:
				}
				_, ok := acquireContext()
				if ok {
					releaseContext()
				}
				time.Sleep(time.Millisecond)
			}
			done <- struct{}{}
		}()
	}

	const bufferSize uint64 = payloadTestBufferSize
	start := getTestBuffer(t, bufferSize)
	EngineInitialize(start, bufferSize, Oneshot, &testSession{})

	// 1 goroutine repeats EngineStart and EngineStop.
	go func() {
	loop:
		for {
			select {
			case <-quit:
				break loop
			default:
			}
			EngineStart(ClearEntireBuffer)
			time.Sleep(time.Millisecond)
			EngineStop()
			time.Sleep(time.Millisecond)
		}
		done <- struct{}{}
	}()

	// Wait for 1 second.
	timer := time.NewTimer(time.Second)
	<-timer.C

	// Signal all goroutines to quit.
	close(quit)

	// Make sure all goutines are done.
	for i := 0; i < n+1; i++ {
		<-done
	}

	if got, want := engineStopErrorCounter, uint64(0); got != want {
		t.Errorf("got engineStopErrorCounter = %d, want = %d", got, want)
	}
}
