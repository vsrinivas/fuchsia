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

	"fidl/fidl/clientsuite"
)

// classifyErr converts an error returned from the FIDL bindings into a FidlErrorKind.
func classifyErr(err error) clientsuite.FidlErrorKind {
	switch err := err.(type) {
	case *zx.Error:
		switch err.Status {
		case zx.ErrPeerClosed:
			return clientsuite.FidlErrorKindChannelPeerClosed
		case zx.ErrInvalidArgs:
			return clientsuite.FidlErrorKindDecodingError
		case zx.ErrNotSupported, zx.ErrNotFound:
			return clientsuite.FidlErrorKindUnexpectedMessage
		}
	}
	return clientsuite.FidlErrorKindOtherError
}

type runnerImpl struct{}

var _ clientsuite.RunnerWithCtx = (*runnerImpl)(nil)

func (*runnerImpl) IsTestEnabled(_ fidl.Context, test clientsuite.Test) (bool, error) {
	isEnabled := func(test clientsuite.Test) bool {
		switch test {
		case clientsuite.TestOneWayStrictSend:
			return false
		case clientsuite.TestOneWayFlexibleSend:
			return false
		case clientsuite.TestTwoWayStrictSend:
			return false
		case clientsuite.TestTwoWayStrictSendMismatchedStrictness:
			return false
		case clientsuite.TestTwoWayStrictErrorSyntaxSendSuccessResponse:
			return false
		case clientsuite.TestTwoWayStrictErrorSyntaxSendErrorResponse:
			return false
		case clientsuite.TestTwoWayStrictErrorSyntaxSendUnknownMethodResponse:
			return false
		case clientsuite.TestTwoWayStrictErrorSyntaxSendMismatchedStrictnessUnknownMethodResponse:
			return false
		case clientsuite.TestTwoWayFlexibleSendSuccessResponse:
			return false
		case clientsuite.TestTwoWayFlexibleSendErrorResponse:
			return false
		case clientsuite.TestTwoWayFlexibleSendUnknownMethodResponse:
			return false
		case clientsuite.TestTwoWayFlexibleSendMismatchedStrictnessUnknownMethodResponse:
			return false
		case clientsuite.TestTwoWayFlexibleSendOtherTransportErrResponse:
			return false
		case clientsuite.TestTwoWayFlexibleSendNonEmptyPayloadSuccessResponse:
			return false
		case clientsuite.TestTwoWayFlexibleSendNonEmptyPayloadUnknownMethodResponse:
			return false
		case clientsuite.TestTwoWayFlexibleErrorSyntaxSendSuccessResponse:
			return false
		case clientsuite.TestTwoWayFlexibleErrorSyntaxSendErrorResponse:
			return false
		case clientsuite.TestTwoWayFlexibleErrorSyntaxSendUnknownMethodResponse:
			return false
		case clientsuite.TestTwoWayFlexibleErrorSyntaxSendMismatchedStrictnessUnknownMethodResponse:
			return false
		case clientsuite.TestTwoWayFlexibleErrorSyntaxSendOtherTransportErrResponse:
			return false
		case clientsuite.TestTwoWayFlexibleErrorSyntaxSendNonEmptyPayloadSuccessResponse:
			return false
		case clientsuite.TestTwoWayFlexibleErrorSyntaxSendNonEmptyPayloadUnknownMethodResponse:
			return false
		case clientsuite.TestReceiveStrictEvent:
			return false
		case clientsuite.TestReceiveStrictEventMismatchedStrictness:
			return false
		case clientsuite.TestReceiveFlexibleEvent:
			return false
		case clientsuite.TestReceiveFlexibleEventMismatchedStrictness:
			return false
		case clientsuite.TestUnknownStrictEventOpenProtocol:
			return false
		case clientsuite.TestUnknownFlexibleEventOpenProtocol:
			return false
		case clientsuite.TestUnknownStrictEventAjarProtocol:
			return false
		case clientsuite.TestUnknownFlexibleEventAjarProtocol:
			return false
		case clientsuite.TestUnknownStrictEventClosedProtocol:
			return false
		case clientsuite.TestUnknownFlexibleEventClosedProtocol:
			return false
		case clientsuite.TestUnknownStrictServerInitiatedTwoWay:
			return false
		case clientsuite.TestUnknownFlexibleServerInitiatedTwoWay:
			return false
		default:
			return true
		}
	}
	return isEnabled(test), nil
}

func (*runnerImpl) CheckAlive(_ fidl.Context) error { return nil }

func (*runnerImpl) CallTwoWayNoPayload(ctx fidl.Context, target clientsuite.ClosedTargetWithCtxInterface) (clientsuite.EmptyResultClassification, error) {
	err := target.TwoWayNoPayload(ctx)
	if err != nil {
		return clientsuite.EmptyResultClassificationWithFidlError(classifyErr(err)), nil
	}
	return clientsuite.EmptyResultClassificationWithSuccess(clientsuite.Empty{}), nil
}

func (*runnerImpl) CallStrictOneWay(_ fidl.Context, target clientsuite.OpenTargetWithCtxInterface) (clientsuite.EmptyResultClassification, error) {
	return clientsuite.EmptyResultClassification{}, errors.New("Go Bindings do not support Open protocols")
}
func (*runnerImpl) CallFlexibleOneWay(_ fidl.Context, target clientsuite.OpenTargetWithCtxInterface) (clientsuite.EmptyResultClassification, error) {
	return clientsuite.EmptyResultClassification{}, errors.New("Go Bindings do not support Open protocols")
}
func (*runnerImpl) CallStrictTwoWay(_ fidl.Context, target clientsuite.OpenTargetWithCtxInterface) (clientsuite.EmptyResultClassification, error) {
	return clientsuite.EmptyResultClassification{}, errors.New("Go Bindings do not support Open protocols")
}
func (*runnerImpl) CallStrictTwoWayErr(_ fidl.Context, target clientsuite.OpenTargetWithCtxInterface) (clientsuite.EmptyResultWithErrorClassification, error) {
	return clientsuite.EmptyResultWithErrorClassification{}, errors.New("Go Bindings do not support Open protocols")
}
func (*runnerImpl) CallFlexibleTwoWay(_ fidl.Context, target clientsuite.OpenTargetWithCtxInterface) (clientsuite.EmptyResultClassification, error) {
	return clientsuite.EmptyResultClassification{}, errors.New("Go Bindings do not support Open protocols")
}
func (*runnerImpl) CallFlexibleTwoWayFields(_ fidl.Context, target clientsuite.OpenTargetWithCtxInterface) (clientsuite.RunnerCallFlexibleTwoWayFieldsResponse, error) {
	return clientsuite.RunnerCallFlexibleTwoWayFieldsResponse{}, errors.New("Go Bindings do not support Open protocols")
}
func (*runnerImpl) CallFlexibleTwoWayErr(_ fidl.Context, target clientsuite.OpenTargetWithCtxInterface) (clientsuite.EmptyResultWithErrorClassification, error) {
	return clientsuite.EmptyResultWithErrorClassification{}, errors.New("Go Bindings do not support Open protocols")
}
func (*runnerImpl) CallFlexibleTwoWayFieldsErr(_ fidl.Context, target clientsuite.OpenTargetWithCtxInterface) (clientsuite.RunnerCallFlexibleTwoWayFieldsErrResponse, error) {
	return clientsuite.RunnerCallFlexibleTwoWayFieldsErrResponse{}, errors.New("Go Bindings do not support Open protocols")
}
func (*runnerImpl) ReceiveClosedEvents(_ fidl.Context, target clientsuite.ClosedTargetWithCtxInterface, reporter clientsuite.ClosedTargetEventReporterWithCtxInterface) error {
	return errors.New("Go bindings event support is too restricted to support the dynsuite event client")
}
func (*runnerImpl) ReceiveAjarEvents(_ fidl.Context, target clientsuite.AjarTargetWithCtxInterface, reporter clientsuite.AjarTargetEventReporterWithCtxInterface) error {
	return errors.New("Go Bindings do not support Ajar protocols")
}
func (*runnerImpl) ReceiveOpenEvents(_ fidl.Context, target clientsuite.OpenTargetWithCtxInterface, reporter clientsuite.OpenTargetEventReporterWithCtxInterface) error {
	return errors.New("Go Bindings do not support Open protocols")
}

func main() {
	log.SetFlags(log.Lshortfile)

	log.Println("Go clientsuite server: starting")
	ctx := component.NewContextFromStartupInfo()
	ctx.OutgoingService.AddService(
		clientsuite.RunnerName,
		func(ctx context.Context, c zx.Channel) error {
			stub := clientsuite.RunnerWithCtxStub{
				Impl: &runnerImpl{},
			}
			go component.Serve(ctx, &stub, c, component.ServeOptions{
				OnError: func(err error) {
					// Panic because the test runner should never fail.
					panic(fmt.Sprintf("clientsuite.Runner errored: %s", err))
				},
			})
			return nil
		},
	)
	log.Println("Go clientsuite server: ready")
	ctx.BindStartupHandle(context.Background())
}
