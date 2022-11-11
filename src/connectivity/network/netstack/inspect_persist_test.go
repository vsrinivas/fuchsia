// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netstack

import (
	"context"
	"syscall/zx/fidl"
	"testing"

	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/diagnostics/persist"
	"fidl/fuchsia/net/neighbor"

	"gvisor.dev/gvisor/pkg/tcpip/faketime"
)

var _ persist.DataPersistenceWithCtx = (*fakePersistServer)(nil)

type fakePersistServer struct {
	requests []string
}

func (server *fakePersistServer) Persist(ctx fidl.Context, tag string) (persist.PersistResult, error) {
	server.requests = append(server.requests, tag)
	return persist.PersistResultQueued, nil
}

func TestPeriodicallyRequestsPersistence(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	clock := faketime.NewManualClock()

	t.Log("setting up")
	req, client, err := persist.NewDataPersistenceWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("persist.NewDataPersistenceWithCtxInterfaceRequest(): %s", err)
	}

	impl := fakePersistServer{}
	stub := persist.DataPersistenceWithCtxStub{Impl: &impl}
	serve_ctx, serve_cancel := context.WithCancel(context.Background())
	defer serve_cancel()
	go component.Serve(serve_ctx, &stub, req.Channel, component.ServeOptions{
		Concurrent: true,
		OnError: func(err error) {
			t.Fatalf("got unexpected error %v", err)
			_ = syslog.WarnTf(neighbor.ViewName, "%s", err)
		},
	})

	runPersistClient(client, ctx, clock)
	expectRequests(t, impl.requests, 1)

	clock.Advance(3 * persistPeriod)
	expectRequests(t, impl.requests, 4)

	cancel()
	clock.Advance(2 * persistPeriod)
	expectRequests(t, impl.requests, 4)
}

func TestExitsAfterRequestChannelFailure(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	clock := faketime.NewManualClock()

	req, client, err := persist.NewDataPersistenceWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("persist.NewDataPersistenceWithCtxInterfaceRequest(): %s", err)
	}

	impl := fakePersistServer{}
	stub := persist.DataPersistenceWithCtxStub{Impl: &impl}
	serve_ctx, serve_cancel := context.WithCancel(context.Background())

	go component.Serve(serve_ctx, &stub, req.Channel, component.ServeOptions{
		Concurrent: true,
		OnError: func(err error) {
			t.Fatalf("got unexpected error %v", err)
			_ = syslog.WarnTf(neighbor.ViewName, "%s", err)
		},
	})

	runPersistClient(client, ctx, clock)
	expectRequests(t, impl.requests, 1)

	serve_cancel()

	clock.Advance(persistPeriod)
	expectRequests(t, impl.requests, 1)

	clock.Advance(persistPeriod)
	expectRequests(t, impl.requests, 1)

	cancel()
}

func expectRequests(t *testing.T, requests []string, want int) {
	t.Helper()
	if got := len(requests); got != want {
		t.Errorf("len(requests) = %d, want %d", got, want)
	}

	for i := 0; i < len(requests); i++ {
		if got, want := requests[i], "netstack-counters"; got != want {
			t.Errorf("requests[%d] = %s, want %s", i, got, want)
		}
	}
}
