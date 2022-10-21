// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package main

import (
	"context"
	"errors"
	"fmt"
	"log"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fidl/serversuite"
	fidlzx "fidl/zx"
)

type closedTargetImpl struct {
	reporter serversuite.ReporterWithCtxInterface
}

var _ serversuite.ClosedTargetWithCtx = (*closedTargetImpl)(nil)

func (t *closedTargetImpl) OneWayNoPayload(_ fidl.Context) error {
	log.Println("serversuite.ClosedTarget OneWayNoPayload() called")
	t.reporter.ReceivedOneWayNoPayload(context.Background())
	return nil
}

func (t *closedTargetImpl) TwoWayNoPayload(_ fidl.Context) error {
	log.Println("serversuite.ClosedTarget TwoWayNoPayload() called")
	return nil
}

func (t *closedTargetImpl) TwoWayStructPayload(_ fidl.Context, v int8) (int8, error) {
	log.Println("serversuite.ClosedTarget TwoWayStructPayload() called")
	return v, nil
}

func (t *closedTargetImpl) TwoWayTablePayload(_ fidl.Context, request serversuite.ClosedTargetTwoWayTablePayloadRequest) (serversuite.ClosedTargetTwoWayTablePayloadResponse, error) {
	log.Println("serversuite.ClosedTarget TwoWayTablePayload() called")
	var response serversuite.ClosedTargetTwoWayTablePayloadResponse
	response.SetV(request.V)
	return response, nil
}

func (t *closedTargetImpl) TwoWayUnionPayload(_ fidl.Context, request serversuite.ClosedTargetTwoWayUnionPayloadRequest) (serversuite.ClosedTargetTwoWayUnionPayloadResponse, error) {
	log.Println("serversuite.ClosedTarget TwoWayUnionPayload() called")
	var response serversuite.ClosedTargetTwoWayUnionPayloadResponse
	response.SetV(request.V)
	return response, nil
}

func (t *closedTargetImpl) TwoWayResult(_ fidl.Context, request serversuite.ClosedTargetTwoWayResultRequest) (serversuite.ClosedTargetTwoWayResultResult, error) {
	log.Println("serversuite.ClosedTarget TwoWayResult() called")
	var result serversuite.ClosedTargetTwoWayResultResult
	switch request.Which() {
	case serversuite.ClosedTargetTwoWayResultRequestPayload:
		result.SetResponse(serversuite.ClosedTargetTwoWayResultResponse{
			Payload: request.Payload,
		})
	case serversuite.ClosedTargetTwoWayResultRequestError:
		result.SetErr(request.Error)
	}
	return result, nil
}

func (t *closedTargetImpl) GetHandleRights(_ fidl.Context, handle zx.Handle) (fidlzx.Rights, error) {
	info, err := handle.GetInfoHandleBasic()
	if err != nil {
		return 0, err
	}
	return fidlzx.Rights(info.Rights), nil
}

func (t *closedTargetImpl) GetSignalableEventRights(_ fidl.Context, handle zx.Event) (fidlzx.Rights, error) {
	info, err := handle.Handle().GetInfoHandleBasic()
	if err != nil {
		return 0, err
	}
	return fidlzx.Rights(info.Rights), nil
}

func (t *closedTargetImpl) EchoAsTransferableSignalableEvent(_ fidl.Context, handle zx.Handle) (zx.Event, error) {
	return zx.Event(handle), nil
}

func (t *closedTargetImpl) CloseWithEpitaph(_ fidl.Context, status int32) error {
	return &component.Epitaph{Status: zx.Status(status)}
}

func (t *closedTargetImpl) ByteVectorSize(_ fidl.Context, v []uint8) (uint32, error) {
	return uint32(len(v)), nil
}

func (t *closedTargetImpl) HandleVectorSize(_ fidl.Context, v []zx.Event) (uint32, error) {
	return uint32(len(v)), nil
}

func (t *closedTargetImpl) CreateNByteVector(_ fidl.Context, n uint32) ([]uint8, error) {
	return make([]uint8, n), nil
}

func (t *closedTargetImpl) CreateNHandleVector(_ fidl.Context, n uint32) ([]zx.Event, error) {
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
		case serversuite.TestOneWayWithNonZeroTxid:
			return false
		case serversuite.TestTwoWayNoPayloadWithZeroTxid:
			return false
		case serversuite.TestBadAtRestFlagsCausesClose:
			return false
		case serversuite.TestBadDynamicFlagsCausesClose:
			return false
		case serversuite.TestSendStrictEvent:
			return false
		case serversuite.TestSendFlexibleEvent:
			return false

		case serversuite.TestReceiveStrictOneWay:
			return false
		case serversuite.TestReceiveStrictOneWayMismatchedStrictness:
			return false
		case serversuite.TestReceiveFlexibleOneWay:
			return false
		case serversuite.TestReceiveFlexibleOneWayMismatchedStrictness:
			return false

		case serversuite.TestStrictTwoWayResponse:
			return false
		case serversuite.TestStrictTwoWayResponseMismatchedStrictness:
			return false
		case serversuite.TestStrictTwoWayNonEmptyResponse:
			return false
		case serversuite.TestStrictTwoWayErrorSyntaxResponse:
			return false
		case serversuite.TestStrictTwoWayErrorSyntaxResponseMismatchedStrictness:
			return false
		case serversuite.TestStrictTwoWayErrorSyntaxNonEmptyResponse:
			return false
		case serversuite.TestFlexibleTwoWayResponse:
			return false
		case serversuite.TestFlexibleTwoWayResponseMismatchedStrictness:
			return false
		case serversuite.TestFlexibleTwoWayNonEmptyResponse:
			return false
		case serversuite.TestFlexibleTwoWayErrorSyntaxResponseSuccessResult:
			return false
		case serversuite.TestFlexibleTwoWayErrorSyntaxResponseErrorResult:
			return false
		case serversuite.TestFlexibleTwoWayErrorSyntaxNonEmptyResponseSuccessResult:
			return false
		case serversuite.TestFlexibleTwoWayErrorSyntaxNonEmptyResponseErrorResult:
			return false

		case serversuite.TestUnknownStrictOneWayOpenProtocol:
			return false
		case serversuite.TestUnknownFlexibleOneWayOpenProtocol:
			return false
		case serversuite.TestUnknownFlexibleOneWayHandleOpenProtocol:
			return false
		case serversuite.TestUnknownStrictTwoWayOpenProtocol:
			return false
		case serversuite.TestUnknownFlexibleTwoWayOpenProtocol:
			return false
		case serversuite.TestUnknownFlexibleTwoWayHandleOpenProtocol:
			return false
		case serversuite.TestUnknownStrictOneWayAjarProtocol:
			return false
		case serversuite.TestUnknownFlexibleOneWayAjarProtocol:
			return false
		case serversuite.TestUnknownStrictTwoWayAjarProtocol:
			return false
		case serversuite.TestUnknownFlexibleTwoWayAjarProtocol:
			return false
		case serversuite.TestUnknownStrictOneWayClosedProtocol:
			return false
		case serversuite.TestUnknownFlexibleOneWayClosedProtocol:
			return false
		case serversuite.TestUnknownStrictTwoWayClosedProtocol:
			return false
		case serversuite.TestUnknownFlexibleTwoWayClosedProtocol:
			return false
		default:
			return true
		}
	}
	return isEnabled(test), nil
}

func (*runnerImpl) Start(
	_ fidl.Context,
	reporter serversuite.ReporterWithCtxInterface,
	target serversuite.AnyTarget) error {

	if target.Which() != serversuite.AnyTargetClosedTarget {
		return errors.New("Go only supports closed protocols")
	}

	go func() {
		stub := serversuite.ClosedTargetWithCtxStub{
			Impl: &closedTargetImpl{
				reporter: reporter,
			},
		}
		component.Serve(context.Background(), &stub, target.ClosedTarget.Channel, component.ServeOptions{
			OnError: func(err error) {
				// Failures are expected as part of tests.
				log.Printf("serversuite.ClosedTarget errored: %s", err)
			},
		})
	}()

	return nil
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
