// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build tracing
// +build tracing

package provider

import (
	"context"
	"errors"
	"fmt"
	"syscall/zx"
	"syscall/zx/zxwait"
	"testing"
	"time"
	"unsafe"

	fidlprovider "fidl/fuchsia/tracing/provider"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/tracing/trace"
)

const (
	vmoSize           uint64 = 4096
	fifoSizeInPackets        = 4
)

type fifoEvent struct {
	packet packet
}

func newTestSession(t *testing.T, vmoSize uint64) (*session, zx.VMO, zx.Handle, chan fifoEvent) {
	t.Helper()
	vmo, err := zx.NewVMO(vmoSize, zx.VMOOption(0))
	if err != nil {
		t.Fatalf("zx.NewVMO failed: %s", err)
	}

	var fifoLocal, fifoRemote zx.Handle
	if status := zx.Sys_fifo_create(fifoSizeInPackets, packetSize, 0, &fifoLocal, &fifoRemote); status != zx.ErrOk {
		t.Fatalf("zx.Sys_fifo_create failed: %s", status)
	}

	ch := make(chan fifoEvent)
	go readFifo(t, fifoLocal, ch)

	s := newSession()

	t.Cleanup(func() {
		select {
		case event, ok := <-ch:
			if ok {
				t.Fatalf("fifo had an unexpected event: %#v", event)
			}
		case <-time.After(10 * time.Second):
			t.Fatalf("fifo is not closed")
		}

		if s.vmo.Handle().IsValid() {
			t.Fatalf("vmo is not closed")
		}
		if s.fifo.IsValid() {
			t.Fatalf("fifo is not closed")
		}
	})

	return s, vmo, fifoRemote, ch
}

func recvFifoPacket(fifo zx.Handle, packet *packet) error {
	const count = 1
	var actual uint
	if status := zx.Sys_fifo_read(fifo, packetSize, unsafe.Pointer(packet), count, &actual); status != zx.ErrOk {
		return &zx.Error{Status: status, Text: "zx.Sys_fifo_read"}
	}
	if actual != count {
		return fmt.Errorf("zx.Sys_fifo_read: actual = %d (want = %d)", actual, count)
	}
	return nil
}

func readFifo(t *testing.T, fifo zx.Handle, ch chan fifoEvent) {
	signals := zx.Signals(zx.SignalFIFOReadable | zx.SignalFIFOPeerClosed)

	for {
		obs, err := zxwait.WaitContext(context.Background(), fifo, signals)
		if err != nil {
			t.Fatalf("zxwait.wait error: %s", err)
		}
		var packet packet
		if obs&zx.SignalFIFOReadable != 0 {
			if err := recvFifoPacket(fifo, &packet); err != nil {
				t.Fatalf("recvFifoPacket error: %s", err)
			}
			ch <- fifoEvent{packet: packet}
		} else if obs&zx.SignalFIFOPeerClosed != 0 {
			close(ch)
			fifo.Close()
			return
		} else {
			t.Fatalf("zxwait.WaitContext returned unexpectedly: obs=%x", obs)
		}
	}
}

func mapBufferHeader(t *testing.T, vmo zx.VMO, vmoSize uint64) *trace.BufferHeader {
	vaddr, err := zx.VMARRoot.Map(0, vmo, 0, vmoSize, zx.VMFlagPermRead)
	if err != nil {
		t.Fatalf("VMARRoot.Map(_, _, _, %d, _) = %s", vmoSize, err)
	}
	return (*trace.BufferHeader)(unsafe.Pointer(uintptr(vaddr)))
}

func TestSessionOneshotInitializeTerminate(t *testing.T) {
	s, vmo, fifo, _ := newTestSession(t, vmoSize)

	mode := fidlprovider.BufferingModeOneshot
	categories := []string{"foo", "bar"}

	if err := s.initializeEngine(mode, vmo, fifo, categories); err != nil {
		t.Fatalf("initializeEngine(%d, _, _, _) = %s", mode, err)
	}

	header := mapBufferHeader(t, vmo, vmoSize)
	if got, want := header.Magic(), trace.BufferHeaderMagic; got != want {
		t.Errorf("got header.Magic() = %#x, want = %#x", got, want)
	}

	if err := s.terminateEngine(); err != nil {
		t.Fatalf("terminateEngine() = %s", err)
	}
}

func TestSessionOneshotInitializeStartTerminate(t *testing.T) {
	s, vmo, fifo, ch := newTestSession(t, vmoSize)

	mode := fidlprovider.BufferingModeOneshot
	categories := []string{"foo", "bar"}

	if err := s.initializeEngine(mode, vmo, fifo, categories); err != nil {
		t.Fatalf("initializeEngine(%d, _, _, %v) = %s", mode, categories, err)
	}

	if err := s.startEngine(fidlprovider.BufferDispositionClearEntire, []string{}); err != nil {
		t.Fatalf("startEngine(_, _) = %s", err)
	}

	event, ok := <-ch
	if !ok {
		t.Fatalf("fifo was closed unexpectedly")
	}
	if got, want := providerRequest(event.packet.request), providerStarted; got != want {
		t.Fatalf("got providerRequest(%d) = %d, want = %d", event.packet.request, got, want)
	}
	if got, want := uint32(event.packet.data32), providerFifoProtocolVersion; got != want {
		t.Fatalf("got event.packet.data32 = %d, want = %d", got, want)
	}

	if err := s.terminateEngine(); err != nil {
		t.Fatalf("terminateEngine() = %s", err)
	}
}

func TestSessionCircularNotSupported(t *testing.T) {
	s, vmo, fifo, _ := newTestSession(t, vmoSize)

	mode := fidlprovider.BufferingModeCircular
	categories := []string{"foo", "bar"}

	if got, want := s.initializeEngine(mode, vmo, fifo, categories), trace.ErrNotSupported; !errors.Is(got, want) {
		t.Fatalf("got initializeEngine(%d, _, _, _) = %s, want = %s", mode, got, want)
	}
}

func TestSessionStreamingNotSupported(t *testing.T) {
	s, vmo, fifo, _ := newTestSession(t, vmoSize)

	mode := fidlprovider.BufferingModeStreaming
	categories := []string{"foo", "bar"}

	if got, want := s.initializeEngine(mode, vmo, fifo, categories), trace.ErrNotSupported; !errors.Is(got, want) {
		t.Fatalf("got initializeEngine(%d, _, _, _) = %s, want = %s", mode, got, want)
	}
}

func TestIsCategoryEnabled(t *testing.T) {
	testCases := []struct {
		testName           string
		initCategories     []string
		enabledCategories  []string
		disabledCategories []string
	}{
		{
			"empty",
			nil,
			[]string{"foo", "bar"}, // any category is enabled.
			nil,
		},
		{
			"foo bar",
			[]string{"foo", "bar"},
			[]string{"foo", "bar"},
			[]string{"baz"},
		},
	}
	for _, tc := range testCases {
		t.Run(tc.testName, func(t *testing.T) {
			s, vmo, fifo, _ := newTestSession(t, vmoSize)

			mode := fidlprovider.BufferingModeOneshot

			if err := s.initializeEngine(mode, vmo, fifo, tc.initCategories); err != nil {
				t.Fatalf("initializeEngine(%d, _, _, %v) = %s", mode, tc.initCategories, err)
			}
			for _, category := range tc.enabledCategories {
				if !s.IsCategoryEnabled(category) {
					t.Errorf("category %s should be enabled", category)
				}
			}
			for _, category := range tc.disabledCategories {
				if s.IsCategoryEnabled(category) {
					t.Errorf("category %s should not be enabled", category)
				}
			}
			if err := s.terminateEngine(); err != nil {
				t.Fatalf("terminateEngine() = %s", err)
			}
		})
	}
}
