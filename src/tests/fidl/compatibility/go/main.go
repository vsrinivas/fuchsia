// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package main

import (
	"context"
	"fmt"
	"log"
	"sync"
	"syscall/zx"
	// "syscall/zx/fdio"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fidl/test/compatibility"
	"fidl/fidl/test/imported"
	// "fidl/fuchsia/io"
	// "fidl/fuchsia/sys"
)

var _ compatibility.EchoWithCtx = (*echoImpl)(nil)

type echoImpl struct {
	ctx *component.Context
}

func (echo *echoImpl) getServer() (*compatibility.EchoWithCtxInterface, error) {
	echoServerEnd, echoClientEnd, err := compatibility.NewEchoWithCtxInterfaceRequest()
	if err != nil {
		return nil, err
	}

	ctx := component.NewContextFromStartupInfo()
	ctx.ConnectToEnvService(echoServerEnd)
	return echoClientEnd, nil
}

func (echo *echoImpl) EchoMinimal(_ fidl.Context, forwardURL string) error {
	if forwardURL == "" {
		return nil
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return err
	}
	if err := echoWithCtxInterface.EchoMinimal(context.Background(), ""); err != nil {
		log.Printf("EchoMinimal failed: %s", err)
		return err
	}
	return nil
}

func (echo *echoImpl) EchoMinimalWithError(_ fidl.Context, forwardURL string, resultVariant compatibility.RespondWith) (compatibility.EchoEchoMinimalWithErrorResult, error) {
	if forwardURL == "" {
		if resultVariant == compatibility.RespondWithErr {
			return compatibility.EchoEchoMinimalWithErrorResultWithErr(0), nil
		} else {
			return compatibility.EchoEchoMinimalWithErrorResultWithResponse(compatibility.EchoEchoMinimalWithErrorResponse{}), nil
		}
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.EchoEchoMinimalWithErrorResult{}, err
	}
	response, err := echoWithCtxInterface.EchoMinimalWithError(context.Background(), "", resultVariant)
	if err != nil {
		log.Printf("EchoMinimal failed: %s", err)
		return compatibility.EchoEchoMinimalWithErrorResult{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoMinimalNoRetVal(_ fidl.Context, forwardURL string) error {
	if forwardURL != "" {
		echoWithCtxInterface, err := echo.getServer()
		if err != nil {
			log.Printf("Connecting to %s failed: %s", forwardURL, err)
			return err
		}
		go func() {
			for {
				if err := echoWithCtxInterface.ExpectEchoMinimalEvent(context.Background()); err != nil {
					log.Fatalf("ExpectEchoMinimalEvent failed: %s while communicating with %s", err, forwardURL)
					return
				}
				mu.Lock()
				for pxy := range mu.proxies {
					_ = pxy.EchoMinimalEvent()
				}
				mu.Unlock()
				break
			}
		}()
		echoWithCtxInterface.EchoMinimalNoRetVal(context.Background(), "")
	} else {
		mu.Lock()
		for pxy := range mu.proxies {
			_ = pxy.EchoMinimalEvent()
		}
		mu.Unlock()
	}
	return nil
}

func (echo *echoImpl) EchoStruct(_ fidl.Context, value compatibility.Struct, forwardURL string) (compatibility.Struct, error) {
	if forwardURL == "" {
		return value, nil
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.Struct{}, err
	}
	response, err := echoWithCtxInterface.EchoStruct(context.Background(), value, "")
	if err != nil {
		log.Printf("EchoStruct failed: %s", err)
		return compatibility.Struct{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoStructWithError(_ fidl.Context, value compatibility.Struct, resultErr compatibility.DefaultEnum, forwardURL string, resultVariant compatibility.RespondWith) (compatibility.EchoEchoStructWithErrorResult, error) {
	if forwardURL == "" {
		if resultVariant == compatibility.RespondWithErr {
			return compatibility.EchoEchoStructWithErrorResultWithErr(resultErr), nil
		} else {
			return compatibility.EchoEchoStructWithErrorResultWithResponse(compatibility.EchoEchoStructWithErrorResponse{
				Value: value,
			}), nil
		}
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.EchoEchoStructWithErrorResult{}, err
	}
	response, err := echoWithCtxInterface.EchoStructWithError(context.Background(), value, resultErr, "", resultVariant)
	if err != nil {
		log.Printf("EchoStruct failed: %s", err)
		return compatibility.EchoEchoStructWithErrorResult{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoStructNoRetVal(_ fidl.Context, value compatibility.Struct, forwardURL string) error {
	if forwardURL != "" {
		echoWithCtxInterface, err := echo.getServer()
		if err != nil {
			log.Printf("Connecting to %s failed: %s", forwardURL, err)
			return err
		}
		go func() {
			for {
				value, err := echoWithCtxInterface.ExpectEchoEvent(context.Background())
				if err != nil {
					log.Fatalf("ExpectEchoEvent failed: %s while communicating with %s", err, forwardURL)
					return
				}
				mu.Lock()
				for pxy := range mu.proxies {
					_ = pxy.EchoEvent(value)
				}
				mu.Unlock()
				break
			}
		}()
		echoWithCtxInterface.EchoStructNoRetVal(context.Background(), value, "")
	} else {
		mu.Lock()
		for pxy := range mu.proxies {
			_ = pxy.EchoEvent(value)
		}
		mu.Unlock()
	}
	return nil
}

func (echo *echoImpl) EchoArrays(_ fidl.Context, value compatibility.ArraysStruct, forwardURL string) (compatibility.ArraysStruct, error) {
	if forwardURL == "" {
		return value, nil
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.ArraysStruct{}, err
	}
	response, err := echoWithCtxInterface.EchoArrays(context.Background(), value, "")
	if err != nil {
		log.Printf("EchoArrays failed: %s", err)
		return compatibility.ArraysStruct{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoArraysWithError(_ fidl.Context, value compatibility.ArraysStruct, resultErr compatibility.DefaultEnum, forwardURL string, resultVariant compatibility.RespondWith) (compatibility.EchoEchoArraysWithErrorResult, error) {
	if forwardURL == "" {
		if resultVariant == compatibility.RespondWithErr {
			return compatibility.EchoEchoArraysWithErrorResultWithErr(resultErr), nil
		} else {
			return compatibility.EchoEchoArraysWithErrorResultWithResponse(compatibility.EchoEchoArraysWithErrorResponse{
				Value: value,
			}), nil
		}
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.EchoEchoArraysWithErrorResult{}, err
	}
	response, err := echoWithCtxInterface.EchoArraysWithError(context.Background(), value, resultErr, "", resultVariant)
	if err != nil {
		log.Printf("EchoArrays failed: %s", err)
		return compatibility.EchoEchoArraysWithErrorResult{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoVectors(_ fidl.Context, value compatibility.VectorsStruct, forwardURL string) (compatibility.VectorsStruct, error) {
	if forwardURL == "" {
		return value, nil
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.VectorsStruct{}, err
	}
	response, err := echoWithCtxInterface.EchoVectors(context.Background(), value, "")
	if err != nil {
		log.Printf("EchoVectors failed: %s", err)
		return compatibility.VectorsStruct{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoVectorsWithError(_ fidl.Context, value compatibility.VectorsStruct, resultErr compatibility.DefaultEnum, forwardURL string, resultVariant compatibility.RespondWith) (compatibility.EchoEchoVectorsWithErrorResult, error) {
	if forwardURL == "" {
		if resultVariant == compatibility.RespondWithErr {
			return compatibility.EchoEchoVectorsWithErrorResultWithErr(resultErr), nil
		} else {
			return compatibility.EchoEchoVectorsWithErrorResultWithResponse(compatibility.EchoEchoVectorsWithErrorResponse{
				Value: value,
			}), nil
		}
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.EchoEchoVectorsWithErrorResult{}, err
	}
	response, err := echoWithCtxInterface.EchoVectorsWithError(context.Background(), value, resultErr, "", resultVariant)
	if err != nil {
		log.Printf("EchoVectors failed: %s", err)
		return compatibility.EchoEchoVectorsWithErrorResult{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoTable(_ fidl.Context, value compatibility.AllTypesTable, forwardURL string) (compatibility.AllTypesTable, error) {
	if forwardURL == "" {
		return value, nil
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.AllTypesTable{}, err
	}
	response, err := echoWithCtxInterface.EchoTable(context.Background(), value, "")
	if err != nil {
		log.Printf("EchoTable failed: %s", err)
		return compatibility.AllTypesTable{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoTableWithError(_ fidl.Context, value compatibility.AllTypesTable, resultErr compatibility.DefaultEnum, forwardURL string, resultVariant compatibility.RespondWith) (compatibility.EchoEchoTableWithErrorResult, error) {
	if forwardURL == "" {
		if resultVariant == compatibility.RespondWithErr {
			return compatibility.EchoEchoTableWithErrorResultWithErr(resultErr), nil
		} else {
			return compatibility.EchoEchoTableWithErrorResultWithResponse(compatibility.EchoEchoTableWithErrorResponse{
				Value: value,
			}), nil
		}
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.EchoEchoTableWithErrorResult{}, err
	}
	response, err := echoWithCtxInterface.EchoTableWithError(context.Background(), value, resultErr, "", resultVariant)
	if err != nil {
		log.Printf("EchoTable failed: %s", err)
		return compatibility.EchoEchoTableWithErrorResult{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoXunions(_ fidl.Context, value []compatibility.AllTypesXunion, forwardURL string) ([]compatibility.AllTypesXunion, error) {
	if forwardURL == "" {
		return value, nil
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return nil, err
	}
	response, err := echoWithCtxInterface.EchoXunions(context.Background(), value, "")
	if err != nil {
		log.Printf("EchoXunions failed: %s", err)
		return nil, err
	}
	return response, nil

}

func (echo *echoImpl) EchoXunionsWithError(_ fidl.Context, value []compatibility.AllTypesXunion, resultErr compatibility.DefaultEnum, forwardURL string, resultVariant compatibility.RespondWith) (compatibility.EchoEchoXunionsWithErrorResult, error) {
	if forwardURL == "" {
		if resultVariant == compatibility.RespondWithErr {
			return compatibility.EchoEchoXunionsWithErrorResultWithErr(resultErr), nil
		} else {
			return compatibility.EchoEchoXunionsWithErrorResultWithResponse(compatibility.EchoEchoXunionsWithErrorResponse{
				Value: value,
			}), nil
		}
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.EchoEchoXunionsWithErrorResult{}, err
	}
	response, err := echoWithCtxInterface.EchoXunionsWithError(context.Background(), value, resultErr, "", resultVariant)
	if err != nil {
		log.Printf("EchoXunions failed: %s", err)
		return compatibility.EchoEchoXunionsWithErrorResult{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoNamedStruct(_ fidl.Context, value imported.SimpleStruct, forwardURL string) (imported.SimpleStruct, error) {
	if forwardURL == "" {
		return value, nil
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return imported.SimpleStruct{}, err
	}
	response, err := echoWithCtxInterface.EchoNamedStruct(context.Background(), value, "")
	if err != nil {
		log.Printf("EchoNamedStruct failed: %s", err)
		return imported.SimpleStruct{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoNamedStructWithError(_ fidl.Context, value imported.SimpleStruct, resultErr uint32, forwardURL string, resultVariant imported.WantResponse) (compatibility.EchoEchoNamedStructWithErrorResult, error) {
	if forwardURL == "" {
		if resultVariant == imported.WantResponseErr {
			return compatibility.EchoEchoNamedStructWithErrorResultWithErr(resultErr), nil
		} else {
			return compatibility.EchoEchoNamedStructWithErrorResultWithResponse(imported.ResponseStruct{
				Value: value,
			}), nil
		}
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.EchoEchoNamedStructWithErrorResult{}, err
	}
	response, err := echoWithCtxInterface.EchoNamedStructWithError(context.Background(), value, resultErr, "", resultVariant)
	if err != nil {
		log.Printf("EchoNamedStruct failed: %s", err)
		return compatibility.EchoEchoNamedStructWithErrorResult{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoNamedStructNoRetVal(_ fidl.Context, value imported.SimpleStruct, forwardURL string) error {
	if forwardURL != "" {
		echoWithCtxInterface, err := echo.getServer()
		if err != nil {
			log.Printf("Connecting to %s failed: %s", forwardURL, err)
			return err
		}
		go func() {
			for {
				value, err := echoWithCtxInterface.ExpectOnEchoNamedEvent(context.Background())
				if err != nil {
					log.Fatalf("ExpectOnEchoStructNamedEvent failed: %s while communicating with %s", err, forwardURL)
					return
				}
				mu.Lock()
				for pxy := range mu.proxies {
					_ = pxy.OnEchoNamedEvent(value)
				}
				mu.Unlock()
				break
			}
		}()
		echoWithCtxInterface.EchoNamedStructNoRetVal(context.Background(), value, "")
	} else {
		mu.Lock()
		for pxy := range mu.proxies {
			_ = pxy.OnEchoNamedEvent(value)
		}
		mu.Unlock()
	}
	return nil
}

func (echo *echoImpl) EchoTablePayload(_ fidl.Context, payload compatibility.RequestTable) (compatibility.ResponseTable, error) {
	if !payload.HasForwardToServer() {
		resp := compatibility.ResponseTable{}
		resp.SetValue(payload.GetValue())
		return resp, nil
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", payload.GetForwardToServer(), err)
		return compatibility.ResponseTable{}, err
	}

	payload.ClearForwardToServer()
	response, err := echoWithCtxInterface.EchoTablePayload(context.Background(), payload)
	if err != nil {
		log.Printf("EchoTablePayload failed: %s", err)
		return compatibility.ResponseTable{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoTablePayloadWithError(_ fidl.Context, payload compatibility.EchoEchoTablePayloadWithErrorRequest) (compatibility.EchoEchoTablePayloadWithErrorResult, error) {
	if !payload.HasForwardToServer() {
		if payload.GetResultVariant() == compatibility.RespondWithErr {
			return compatibility.EchoEchoTablePayloadWithErrorResultWithErr(payload.GetResultErr()), nil
		} else {
			resp := compatibility.ResponseTable{}
			resp.SetValue(payload.GetValue())
			return compatibility.EchoEchoTablePayloadWithErrorResultWithResponse(resp), nil
		}
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", payload.GetForwardToServer(), err)
		return compatibility.EchoEchoTablePayloadWithErrorResult{}, err
	}

	payload.ClearForwardToServer()
	response, err := echoWithCtxInterface.EchoTablePayloadWithError(context.Background(), payload)
	if err != nil {
		log.Printf("EchoTablePayloadWithError failed: %s", err)
		return compatibility.EchoEchoTablePayloadWithErrorResult{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoTablePayloadNoRetVal(_ fidl.Context, payload compatibility.RequestTable) error {
	if payload.HasForwardToServer() {
		forwardURL := payload.GetForwardToServer()
		payload.ClearForwardToServer()
		echoWithCtxInterface, err := echo.getServer()
		if err != nil {
			log.Printf("Connecting to %s failed: %s", forwardURL, err)
			return err
		}
		go func() {
			for {
				value, err := echoWithCtxInterface.ExpectOnEchoTablePayloadEvent(context.Background())
				if err != nil {
					log.Fatalf("ExpectOnEchoTablePayloadEvent failed: %s while communicating with %s", err, forwardURL)
					return
				}
				mu.Lock()
				for pxy := range mu.proxies {
					_ = pxy.OnEchoTablePayloadEvent(value)
				}
				mu.Unlock()
				break
			}
		}()
		echoWithCtxInterface.EchoTablePayloadNoRetVal(context.Background(), payload)
	} else {
		resp := compatibility.ResponseTable{}
		resp.SetValue(payload.GetValue())
		mu.Lock()
		for pxy := range mu.proxies {
			_ = pxy.OnEchoTablePayloadEvent(resp)
		}
		mu.Unlock()
	}
	return nil
}

func (echo *echoImpl) EchoTableRequestComposed(_ fidl.Context, payload imported.ComposedEchoTableRequestComposedRequest) (imported.SimpleStruct, error) {
	if !payload.HasForwardToServer() {
		return imported.SimpleStruct{
			F1: true,
			F2: payload.GetValue(),
		}, nil
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", payload.GetForwardToServer(), err)
		return imported.SimpleStruct{}, err
	}
	payload.ClearForwardToServer()
	response, err := echoWithCtxInterface.EchoTableRequestComposed(context.Background(), payload)
	if err != nil {
		log.Printf("EchoTableRequestComposed failed: %s", err)
		return imported.SimpleStruct{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoUnionPayload(_ fidl.Context, payload compatibility.RequestUnion) (compatibility.ResponseUnion, error) {
	var forwardURL string
	if payload.Which() == compatibility.RequestUnionUnsigned {
		if payload.Unsigned.ForwardToServer == "" {
			return compatibility.ResponseUnionWithUnsigned(payload.Unsigned.Value), nil
		}
		forwardURL = payload.Unsigned.ForwardToServer
		payload.Unsigned.ForwardToServer = ""
	} else if payload.Which() == compatibility.RequestUnionSigned {
		if payload.Signed.ForwardToServer == "" {
			return compatibility.ResponseUnionWithSigned(payload.Signed.Value), nil
		}
		forwardURL = payload.Signed.ForwardToServer
		payload.Signed.ForwardToServer = ""
	} else {
		log.Printf("Unknown ordinal for union: %d", payload.Which())
		return compatibility.ResponseUnion{}, fmt.Errorf("unknown union ordinal")
	}

	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.ResponseUnion{}, err
	}

	response, err := echoWithCtxInterface.EchoUnionPayload(context.Background(), payload)
	if err != nil {
		log.Printf("EchoUnionPayload failed: %s", err)
		return compatibility.ResponseUnion{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoUnionPayloadWithError(_ fidl.Context, payload compatibility.EchoEchoUnionPayloadWithErrorRequest) (compatibility.EchoEchoUnionPayloadWithErrorResult, error) {
	var forwardURL string
	if payload.Which() == compatibility.EchoEchoUnionPayloadWithErrorRequestUnsigned {
		forwardURL = payload.Unsigned.ForwardToServer
		payload.Unsigned.ForwardToServer = ""
		if forwardURL == "" {
			if payload.Unsigned.ResultVariant == compatibility.RespondWithErr {
				return compatibility.EchoEchoUnionPayloadWithErrorResultWithErr(payload.Unsigned.ResultErr), nil
			}
			return compatibility.EchoEchoUnionPayloadWithErrorResultWithResponse(compatibility.ResponseUnionWithUnsigned(payload.Unsigned.Value)), nil
		}
	} else if payload.Which() == compatibility.EchoEchoUnionPayloadWithErrorRequestSigned {
		forwardURL = payload.Signed.ForwardToServer
		payload.Signed.ForwardToServer = ""
		if forwardURL == "" {
			if payload.Signed.ResultVariant == compatibility.RespondWithErr {
				return compatibility.EchoEchoUnionPayloadWithErrorResultWithErr(payload.Signed.ResultErr), nil
			}
			return compatibility.EchoEchoUnionPayloadWithErrorResultWithResponse(compatibility.ResponseUnionWithSigned(payload.Signed.Value)), nil
		}
	} else {
		log.Printf("Unknown ordinal for union: %d", payload.Which())
		return compatibility.EchoEchoUnionPayloadWithErrorResult{}, fmt.Errorf("unknown union ordinal")
	}

	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.EchoEchoUnionPayloadWithErrorResult{}, err
	}

	response, err := echoWithCtxInterface.EchoUnionPayloadWithError(context.Background(), payload)
	if err != nil {
		log.Printf("EchoUnionPayloadWithError failed: %s", err)
		return compatibility.EchoEchoUnionPayloadWithErrorResult{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoUnionPayloadNoRetVal(_ fidl.Context, payload compatibility.RequestUnion) error {
	var resp compatibility.ResponseUnion
	var forwardURL string
	if payload.Which() == compatibility.RequestUnionUnsigned {
		if payload.Unsigned.ForwardToServer == "" {
			resp = compatibility.ResponseUnionWithUnsigned(payload.Unsigned.Value)
		}
		forwardURL = payload.Unsigned.ForwardToServer
		payload.Unsigned.ForwardToServer = ""
	} else if payload.Which() == compatibility.RequestUnionSigned {
		if payload.Signed.ForwardToServer == "" {
			resp = compatibility.ResponseUnionWithSigned(payload.Signed.Value)
		}
		forwardURL = payload.Signed.ForwardToServer
		payload.Signed.ForwardToServer = ""
	} else {
		log.Printf("Unknown ordinal for union: %d", payload.Which())
		return fmt.Errorf("unknown union ordinal")
	}

	if forwardURL != "" {
		echoWithCtxInterface, err := echo.getServer()
		if err != nil {
			log.Printf("Connecting to %s failed: %s", forwardURL, err)
			return err
		}
		go func() {
			for {
				value, err := echoWithCtxInterface.ExpectOnEchoUnionPayloadEvent(context.Background())
				if err != nil {
					log.Fatalf("ExpectOnEchoUnionPayloadEvent failed: %s while communicating with %s", err, forwardURL)
					return
				}
				mu.Lock()
				for pxy := range mu.proxies {
					_ = pxy.OnEchoUnionPayloadEvent(value)
				}
				mu.Unlock()
				break
			}
		}()
		echoWithCtxInterface.EchoUnionPayloadNoRetVal(context.Background(), payload)
	} else {
		mu.Lock()
		for pxy := range mu.proxies {
			_ = pxy.OnEchoUnionPayloadEvent(resp)
		}
		mu.Unlock()
	}
	return nil
}

func (echo *echoImpl) EchoUnionResponseWithErrorComposed(_ fidl.Context, value int64, wantAbsoluteValue bool, forwardURL string, resultErr uint32, resultVariant imported.WantResponse) (imported.ComposedEchoUnionResponseWithErrorComposedResult, error) {
	if forwardURL == "" {
		if resultVariant == imported.WantResponseErr {
			return imported.ComposedEchoUnionResponseWithErrorComposedResultWithErr(resultErr), nil
		} else if wantAbsoluteValue {
			abs := value
			if value < 0 {
				abs = value * -1
			}
			return imported.ComposedEchoUnionResponseWithErrorComposedResultWithResponse(
				imported.ComposedEchoUnionResponseWithErrorComposedResponseWithUnsigned(uint64(abs)),
			), nil
		} else {
			return imported.ComposedEchoUnionResponseWithErrorComposedResultWithResponse(
				imported.ComposedEchoUnionResponseWithErrorComposedResponseWithSigned(value),
			), nil
		}
	}
	echoWithCtxInterface, err := echo.getServer()
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return imported.ComposedEchoUnionResponseWithErrorComposedResult{}, err
	}
	response, err := echoWithCtxInterface.EchoUnionResponseWithErrorComposed(context.Background(), value, wantAbsoluteValue, "", resultErr, resultVariant)
	if err != nil {
		log.Printf("EchoUnionResponseWithErrorComposed failed: %s", err)
		return imported.ComposedEchoUnionResponseWithErrorComposedResult{}, err
	}
	return response, nil
}

var mu struct {
	sync.Mutex

	proxies map[*compatibility.EchoEventProxy]struct{}
}

func init() {
	mu.proxies = make(map[*compatibility.EchoEventProxy]struct{})
}

func main() {
	log.SetFlags(log.Lshortfile)

	ctx := component.NewContextFromStartupInfo()

	stub := compatibility.EchoWithCtxStub{Impl: &echoImpl{ctx: ctx}}
	ctx.OutgoingService.AddService(
		compatibility.EchoName,
		func(ctx context.Context, c zx.Channel) error {
			pxy := compatibility.EchoEventProxy{Channel: c}
			mu.Lock()
			mu.proxies[&pxy] = struct{}{}
			mu.Unlock()

			go func() {
				defer func() {
					mu.Lock()
					delete(mu.proxies, &pxy)
					mu.Unlock()
				}()
				component.Serve(ctx, &stub, c, component.ServeOptions{
					OnError: func(err error) {
						log.Print(err)
					},
				})
			}()
			return nil
		},
	)

	ctx.BindStartupHandle(context.Background())
}
