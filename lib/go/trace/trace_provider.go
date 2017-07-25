// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package trace provides definition and methods for TraceProvider and
// Registers it with TraceRegistry
package trace

import (
	"errors"
	"os"
	"path/filepath"

	"fidl/bindings"
	"syscall/mx"

	"apps/tracing/services/trace_provider"
	"apps/tracing/services/trace_registry"
)

type Provider struct {
	stub *bindings.Stub
}

func InitTraceProvider(traceRegistryProxy *trace_registry.Proxy, setting Setting) *Provider {
	r, p := trace_provider.NewChannel()
	tp := &Provider{}
	stub := trace_provider.NewStub(r, tp, bindings.GetAsyncWaiter())
	tp.stub = stub

	label := setting.ProviderLabel
	if label == "" {
		label = filepath.Base(os.Args[0])
	}

	traceRegistryProxy.RegisterTraceProvider(p, &label)
	return tp
}

func (tp *Provider) Start(inBuffer mx.Handle, inFence mx.Handle, inCategories []string) (bool, error) {
	return false, errors.New("Not Implemented")
}

func (tp *Provider) Stop() error {
	return errors.New("Not Implemented")
}

func (tp *Provider) Dump(inOutput mx.Handle) error {
	return errors.New("Not Implemented")
}

func (tp *Provider) Destroy() {
	tp.stub.Close()
}
