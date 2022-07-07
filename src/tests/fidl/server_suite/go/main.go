// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package main

import (
	"context"
	"fmt"
	"log"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fidl/serversuite"
)

type targetImpl struct {
	reporter serversuite.ReporterWithCtxInterface
}

var _ serversuite.TargetWithCtx = (*targetImpl)(nil)

func (t *targetImpl) OneWayNoPayload(_ fidl.Context) error {
	log.Println("serversuite.Target OneWayNoPayload() called")
	t.reporter.ReceivedOneWayNoPayload(context.Background())
	return nil
}

type runnerImpl struct{}

var _ serversuite.RunnerWithCtx = (*runnerImpl)(nil)

func (*runnerImpl) IsTestEnabled(_ fidl.Context, test serversuite.Test) (bool, error) {
	return test != serversuite.TestOneWayWithNonZeroTxid, nil
}

func (*runnerImpl) Start(
	_ fidl.Context,
	reporter serversuite.ReporterWithCtxInterface) (serversuite.TargetWithCtxInterface, error) {

	clientEnd, serverEnd, err := zx.NewChannel(0)
	if err != nil {
		return serversuite.TargetWithCtxInterface{}, err
	}

	go func() {
		stub := serversuite.TargetWithCtxStub{
			Impl: &targetImpl{
				reporter: reporter,
			},
		}
		component.Serve(context.Background(), &stub, serverEnd, component.ServeOptions{
			OnError: func(err error) {
				// Failures are expected as part of tests.
				log.Printf("serversuite.Target errored: %s", err)
			},
		})
	}()

	return serversuite.TargetWithCtxInterface{Channel: clientEnd}, nil
}

func (*runnerImpl) CheckAlive(_ fidl.Context) error { return nil }

func main() {
	log.SetFlags(log.Lshortfile)

	log.Println("Go serversuite server: starting")
	ctx := component.NewContextFromStartupInfo()
	ctx.OutgoingService.AddService(
		serversuite.RunnerName,
		func(ctx context.Context, c zx.Channel) error {
			stub := serversuite.RunnerWithCtxStub{
				Impl: &runnerImpl{},
			}
			go component.Serve(ctx, &stub, c, component.ServeOptions{
				OnError: func(err error) {
					// Panic because the test runner should never fail.
					panic(fmt.Sprintf("serversuite.Runner errored: %s", err))
				},
			})
			return nil
		},
	)
	log.Println("Go serversuite server: ready")
	ctx.BindStartupHandle(context.Background())
}
