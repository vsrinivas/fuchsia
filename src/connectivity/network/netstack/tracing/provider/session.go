// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build tracing
// +build tracing

// This file implements a Go equivalent of:
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/trace-provider/session.h
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/trace-provider/session.cc

package provider

import (
	"fmt"
	"syscall/zx"
	"unsafe"

	fidlprovider "fidl/fuchsia/tracing/provider"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/tracing/trace"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"
)

// #include <lib/trace-provider/provider.h>
import "C"

var _ trace.Handler = (*session)(nil)

// The ownership of session is passed to the trace engine once trace.EngineInitialize
// is successful. It is returned to the provider after terminateEngine is
// called. Once a session is no longer needed, finalize should be called on it.
type session struct {
	vmo        zx.VMO
	fifo       zx.Handle
	categories map[string]struct{}

	vaddr   zx.Vaddr
	vmoSize uint64
}

func newSession() *session {
	return &session{
		categories: make(map[string]struct{}),
	}
}

func (s *session) initializeEngine(mode fidlprovider.BufferingMode, vmo zx.VMO, fifo zx.Handle, categories []string) (err error) {
	defer func() {
		if err != nil {
			s.finalize()
		}
	}()

	switch state := trace.EngineState(); state {
	case trace.Stopped:
	case trace.Stopping:
		return fmt.Errorf("cannot initialize engine, still stopping from previous trace")
	case trace.Started:
		return fmt.Errorf("engine is alreay initialized")
	default:
		panic(fmt.Sprintf("unknown engine state (%d)", state))
	}

	s.vmo = vmo
	s.fifo = fifo
	for _, category := range categories {
		s.categories[category] = struct{}{}
	}

	sz, err := vmo.Size()
	if err != nil {
		return fmt.Errorf("vmo.Size failed: %s", err)
	}
	s.vmoSize = sz
	vaddr, err := zx.VMARRoot.Map(0, s.vmo, 0, s.vmoSize, zx.VMFlagPermRead|zx.VMFlagPermWrite)
	if err != nil {
		return fmt.Errorf("VMARRoot.Map failed: %s", err)
	}
	s.vaddr = vaddr

	var bufferingMode trace.BufferingMode
	switch mode {
	case fidlprovider.BufferingModeOneshot:
		bufferingMode = trace.Oneshot
	case fidlprovider.BufferingModeCircular:
		bufferingMode = trace.Circular
	case fidlprovider.BufferingModeStreaming:
		bufferingMode = trace.Streaming
	default:
		panic(fmt.Sprintf("unknown BufferingMode (%d)", mode))
	}
	if err := trace.EngineInitialize(uintptr(s.vaddr), s.vmoSize, bufferingMode, s); err != nil {
		return err
	}
	return nil
}

func (s *session) finalize() {
	if s.vmoSize > 0 {
		if err := zx.VMARRoot.Unmap(s.vaddr, s.vmoSize); err != nil {
			_ = syslog.ErrorTf(tag, "VMARRoot.UnMap failed: %s", err)
		}
	}
	s.vaddr = 0
	s.vmoSize = 0
	if err := s.vmo.Close(); err != nil {
		_ = syslog.ErrorTf(tag, "vmo.Close() failed: %s", err)
	}
	if s.fifo.IsValid() {
		if err := s.fifo.Close(); err != nil {
			_ = syslog.ErrorTf(tag, "fifo.Close() failed: %s", err)
		}
	}
}

func (s *session) startEngine(disposition fidlprovider.BufferDisposition, additionalCategories []string) error {
	switch state := trace.EngineState(); state {
	case trace.Stopped:
	case trace.Stopping:
		return fmt.Errorf("cannot start engine, still stopping from previous trace")
	case trace.Started:
		// Ignore.
		return nil
	default:
		panic(fmt.Sprintf("unknown engine state (%d)", state))
	}

	var startMode trace.StartMode
	switch disposition {
	case fidlprovider.BufferDispositionClearEntire:
		startMode = trace.ClearEntireBuffer
	case fidlprovider.BufferDispositionClearNondurable:
		startMode = trace.ClearNonDurableBuffer
	case fidlprovider.BufferDispositionRetain:
		startMode = trace.RetainBuffer
	default:
		panic(fmt.Sprintf("unknown disposition (%d)", disposition))
	}
	// TODO(https://fxbug.dev/22973): Add support for additional categories.
	if err := trace.EngineStart(startMode); err != nil {
		return err
	}
	return nil
}

func (s *session) stopEngine() error {
	trace.EngineStop()
	return nil
}

func (s *session) terminateEngine() error {
	trace.EngineTerminate()
	s.finalize()
	return nil
}

func (s *session) IsCategoryEnabled(category string) bool {
	if len(s.categories) == 0 {
		return true
	}
	_, ok := s.categories[category]
	return ok
}

type packet = C.struct_trace_provider_packet

const packetSize = uint(unsafe.Sizeof(packet{}))

type providerRequest int

const (
	providerStarted    providerRequest = C.TRACE_PROVIDER_STARTED
	providerStopped                    = C.TRACE_PROVIDER_STOPPED
	providerSaveBuffer                 = C.TRACE_PROVIDER_SAVE_BUFFER
	providerAlert                      = C.TRACE_PROVIDER_ALERT

	providerFifoProtocolVersion uint32 = C.TRACE_PROVIDER_FIFO_PROTOCOL_VERSION
)

func (s *session) TraceStarted() {
	s.sendFifoPacket(&packet{
		request: C.TRACE_PROVIDER_STARTED,
		data32:  C.TRACE_PROVIDER_FIFO_PROTOCOL_VERSION,
	})
}

func (s *session) TraceStopped() {
	s.sendFifoPacket(&packet{
		request: C.TRACE_PROVIDER_STOPPED,
	})
}

func (s *session) TraceTerminated() {}

func (s *session) NotifyBufferFull(wrapperCount uint32, offset uint64) {
	s.sendFifoPacket(&packet{
		request: C.TRACE_PROVIDER_SAVE_BUFFER,
		data32:  C.uint32_t(wrapperCount),
		data64:  C.uint64_t(offset),
	})
}

func (s *session) SendAlert() {
	s.sendFifoPacket(&packet{
		request: C.TRACE_PROVIDER_ALERT,
	})
}

func (s *session) sendFifoPacket(packet *packet) {
	const count = 1
	var actual uint
	if status := zx.Sys_fifo_write(s.fifo, packetSize, unsafe.Pointer(packet), count, &actual); status != zx.ErrOk {
		_ = syslog.ErrorTf(tag, "SendFifoPacket: %s", &zx.Error{Status: status, Text: "zx.Sys_fifo_write"})
	}
	if actual != count {
		_ = syslog.ErrorTf(tag, "SendFifoPacket: actual=%d (want=%d)", actual, count)
	}
}
