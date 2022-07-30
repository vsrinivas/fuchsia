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
	fidlzx "fidl/zx"
)

type targetImpl struct {
	reporter serversuite.ReporterWithCtxInterface
	channel  zx.Channel
}

var _ serversuite.TargetWithCtx = (*targetImpl)(nil)

func (t *targetImpl) OneWayNoPayload(_ fidl.Context) error {
	log.Println("serversuite.Target OneWayNoPayload() called")
	t.reporter.ReceivedOneWayNoPayload(context.Background())
	return nil
}

func (t *targetImpl) TwoWayNoPayload(_ fidl.Context) error {
	log.Println("serversuite.Target TwoWayNoPayload() called")
	return nil
}

func (t *targetImpl) TwoWayStructPayload(_ fidl.Context, v int8) (int8, error) {
	log.Println("serversuite.Target TwoWayStructPayload() called")
	return v, nil
}

func (t *targetImpl) TwoWayTablePayload(_ fidl.Context, request serversuite.TargetTwoWayTablePayloadRequest) (serversuite.TargetTwoWayTablePayloadResponse, error) {
	log.Println("serversuite.Target TwoWayTablePayload() called")
	var response serversuite.TargetTwoWayTablePayloadResponse
	response.SetV(request.V)
	return response, nil
}

func (t *targetImpl) TwoWayUnionPayload(_ fidl.Context, request serversuite.TargetTwoWayUnionPayloadRequest) (serversuite.TargetTwoWayUnionPayloadResponse, error) {
	log.Println("serversuite.Target TwoWayUnionPayload() called")
	var response serversuite.TargetTwoWayUnionPayloadResponse
	response.SetV(request.V)
	return response, nil
}

func (t *targetImpl) TwoWayResult(_ fidl.Context, request serversuite.TargetTwoWayResultRequest) (serversuite.TargetTwoWayResultResult, error) {
	log.Println("serversuite.Target TwoWayResult() called")
	var result serversuite.TargetTwoWayResultResult
	switch request.Which() {
	case serversuite.TargetTwoWayResultRequestPayload:
		result.SetResponse(serversuite.TargetTwoWayResultResponse{
			Payload: request.Payload,
		})
	case serversuite.TargetTwoWayResultRequestError:
		result.SetErr(request.Error)
	}
	return result, nil
}

func (t *targetImpl) GetHandleRights(_ fidl.Context, handle zx.Handle) (fidlzx.Rights, error) {
	info, err := handle.GetInfoHandleBasic()
	if err != nil {
		return 0, err
	}
	return fidlzx.Rights(info.Rights), nil
}

func (t *targetImpl) GetSignalableEventRights(_ fidl.Context, handle zx.Event) (fidlzx.Rights, error) {
	info, err := handle.Handle().GetInfoHandleBasic()
	if err != nil {
		return 0, err
	}
	return fidlzx.Rights(info.Rights), nil
}

func (t *targetImpl) EchoAsTransferableSignalableEvent(_ fidl.Context, handle zx.Handle) (zx.Event, error) {
	return zx.Event(handle), nil
}

func (t *targetImpl) CloseWithEpitaph(_ fidl.Context, status int32) error {
	panic("unimplemented")
}

func (t *targetImpl) ByteVectorSize(_ fidl.Context, v []uint8) (uint32, error) {
	return uint32(len(v)), nil
}

func (t *targetImpl) HandleVectorSize(_ fidl.Context, v []zx.Event) (uint32, error) {
	return uint32(len(v)), nil
}

func (t *targetImpl) CreateNByteVector(_ fidl.Context, n uint32) ([]uint8, error) {
	return make([]uint8, n), nil
}

func (t *targetImpl) CreateNHandleVector(_ fidl.Context, n uint32) ([]zx.Event, error) {
	out := make([]zx.Event, n)
	for i := range out {
		var err error
		out[i], err = zx.NewEvent(0)
		if err != nil {
			return nil, err
		}
	}
	return out, nil
}

type runnerImpl struct{}

var _ serversuite.RunnerWithCtx = (*runnerImpl)(nil)

func (*runnerImpl) IsTestEnabled(_ fidl.Context, test serversuite.Test) (bool, error) {
	isEnabled := func(test serversuite.Test) bool {
		switch test {
		case serversuite.TestTwoWayTablePayload:
			return false
		case serversuite.TestTwoWayUnionPayload:
			return false
		case serversuite.TestOneWayWithNonZeroTxid:
			return false
		case serversuite.TestTwoWayNoPayloadWithZeroTxid:
			return false
		case serversuite.TestBadAtRestFlagsCausesClose:
			return false
		case serversuite.TestBadDynamicFlagsCausesClose:
			return false
		case serversuite.TestServerSendsEpitaph:
			return false
		default:
			return true
		}
	}
	return isEnabled(test), nil
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
				channel:  serverEnd,
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
