// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package trace provides definition and methods for TraceProvider and
// Registers it with TraceRegistry
package trace

import (
	"fmt"

	"application/lib/app/context"
	"fidl/bindings"

	"apps/tracing/services/trace_registry"
)

// singleton
var traceProvider *Provider

func InitializeTracerUsingFlag(ctx *context.Context, ts Setting) error {
	var err error
	if err, ts = ParseTraceSettingsUsingFlag(ts); err != nil {
		return err
	}
	InitializeTracer(ctx, ts)
	return nil
}

func InitializeTracer(ctx *context.Context, ts Setting) {
	var p *trace_registry.Proxy
	r, p := p.NewRequest(bindings.GetAsyncWaiter())
	ctx.ConnectToEnvService(r)
	InitializeTracerUsingRegistry(p, ts)
}

func InitializeTracerUsingRegistry(p *trace_registry.Proxy, ts Setting) {
	if traceProvider != nil {
		fmt.Println("Tracer is already initialized")
		return
	}
	traceProvider = InitTraceProvider(p, ts)
}

func DestroyTracer() {
	traceProvider.Destroy()
	traceProvider = nil
}
