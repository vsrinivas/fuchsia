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
		// This case will forever be false, as it is intended to validate the "test
		// disabling" functionality of the runner itself.
		case serversuite.TestIgnoreDisabled:
			return false

		case serversuite.TestOneWayWithNonZeroTxid,
			serversuite.TestTwoWayNoPayloadWithZeroTxid,
			serversuite.TestSendStrictEvent,
			serversuite.TestSendFlexibleEvent,
			serversuite.TestReceiveStrictOneWay,
			serversuite.TestReceiveStrictOneWayMismatchedStrictness,
			serversuite.TestReceiveFlexibleOneWay,
			serversuite.TestReceiveFlexibleOneWayMismatchedStrictness,
			serversuite.TestStrictTwoWayResponse,
			serversuite.TestStrictTwoWayResponseMismatchedStrictness,
			serversuite.TestStrictTwoWayNonEmptyResponse,
			serversuite.TestStrictTwoWayErrorSyntaxResponse,
			serversuite.TestStrictTwoWayErrorSyntaxResponseMismatchedStrictness,
			serversuite.TestStrictTwoWayErrorSyntaxNonEmptyResponse,
			serversuite.TestFlexibleTwoWayResponse,
			serversuite.TestFlexibleTwoWayResponseMismatchedStrictness,
			serversuite.TestFlexibleTwoWayNonEmptyResponse,
			serversuite.TestFlexibleTwoWayErrorSyntaxResponseSuccessResult,
			serversuite.TestFlexibleTwoWayErrorSyntaxResponseErrorResult,
			serversuite.TestFlexibleTwoWayErrorSyntaxNonEmptyResponseSuccessResult,
			serversuite.TestFlexibleTwoWayErrorSyntaxNonEmptyResponseErrorResult,
			serversuite.TestUnknownStrictOneWayOpenProtocol,
			serversuite.TestUnknownFlexibleOneWayOpenProtocol,
			serversuite.TestUnknownFlexibleOneWayHandleOpenProtocol,
			serversuite.TestUnknownStrictTwoWayOpenProtocol,
			serversuite.TestUnknownFlexibleTwoWayOpenProtocol,
			serversuite.TestUnknownFlexibleTwoWayHandleOpenProtocol,
			serversuite.TestUnknownStrictOneWayAjarProtocol,
			serversuite.TestUnknownFlexibleOneWayAjarProtocol,
			serversuite.TestUnknownStrictTwoWayAjarProtocol,
			serversuite.TestUnknownFlexibleTwoWayAjarProtocol,
			serversuite.TestUnknownStrictOneWayClosedProtocol,
			serversuite.TestUnknownFlexibleOneWayClosedProtocol,
			serversuite.TestUnknownStrictTwoWayClosedProtocol,
			serversuite.TestUnknownFlexibleTwoWayClosedProtocol,
			serversuite.TestBadDecodeByteOverflowFlagSetOnSmallMessage,
			serversuite.TestBadDecodeByteOverflowFlagUnsetOnLargeMessage,
			serversuite.TestBadDecodeLargeMessageInfoOmitted,
			serversuite.TestBadDecodeLargeMessageInfoTooSmall,
			serversuite.TestBadDecodeLargeMessageInfoTooLarge,
			serversuite.TestBadDecodeLargeMessageInfoTopHalfUnzeroed,
			serversuite.TestBadDecodeLargeMessageInfoByteCountIsZero,
			serversuite.TestBadDecodeLargeMessageInfoByteCountTooSmall,
			serversuite.TestBadDecodeLargeMessageInfoByteCountNotEqualToBound,
			serversuite.TestBadDecodeNoHandles,
			serversuite.TestBadDecodeTooFewHandles,
			serversuite.TestBadDecode64HandleLargeMessage,
			serversuite.TestBadDecodeLastHandleNotVmo,
			serversuite.TestBadDecodeLastHandleInsufficientRights,
			serversuite.TestBadDecodeVmoTooSmall,
			serversuite.TestBadDecodeVmoTooLarge,
			serversuite.TestBadEncode64HandleLargeMessage,
			serversuite.TestGoodDecodeBoundedMaybeLargeMessage,
			serversuite.TestGoodDecodeSemiBoundedUnknowableLargeMessage,
			serversuite.TestGoodDecodeSemiBoundedMaybeLargeMessage,
			serversuite.TestGoodDecodeUnboundedLargeMessage,
			serversuite.TestGoodDecode63HandleLargeMessage,
			serversuite.TestGoodDecodeUnknownSmallMessage,
			serversuite.TestGoodDecodeUnknownLargeMessage,
			serversuite.TestGoodEncodeBoundedMaybeLargeMessage,
			serversuite.TestGoodEncodeSemiBoundedMaybeLargeMessage,
			serversuite.TestGoodEncodeUnboundedLargeMessage,
			serversuite.TestGoodEncode63HandleLargeMessage:
			return false

		case serversuite.TestV1TwoWayNoPayload, serversuite.TestV1TwoWayStructPayload:
			// TODO(fxbug.dev/99738): Go bindings should reject V1 wire format.
			return false

		case serversuite.TestGoodDecodeBoundedKnownSmallMessage,
			serversuite.TestGoodDecodeBoundedMaybeSmallMessage,
			serversuite.TestGoodDecodeSemiBoundedUnknowableSmallMessage,
			serversuite.TestGoodDecodeSemiBoundedMaybeSmallMessage,
			serversuite.TestGoodDecodeUnboundedSmallMessage,
			serversuite.TestGoodDecode64HandleSmallMessage,
			serversuite.TestGoodEncodeBoundedKnownSmallMessage,
			serversuite.TestGoodEncodeBoundedMaybeSmallMessage,
			serversuite.TestGoodEncodeSemiBoundedKnownSmallMessage,
			serversuite.TestGoodEncodeSemiBoundedMaybeSmallMessage,
			serversuite.TestGoodEncodeUnboundedSmallMessage,
			serversuite.TestGoodEncode64HandleSmallMessage:
			// TODO(fxbug.dev/114266): Though the Go bindings don't support large
			// messages, these messages are all "small", and so should be successfully
			// handled the these bindings. These cases are especially useful since
			// they are good limit tests (64 handles, max message size, etc).
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

	if target.Which() == serversuite.AnyTargetLargeMessageTarget {
		// TODO(fxbug.dev/114266): Test that go properly reports large messages when
		// it encounters them.
		return errors.New("Go does not support large messages")
	}
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
