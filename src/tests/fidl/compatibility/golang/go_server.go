// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package main

import (
	"context"
	"log"
	"sync"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fidl/test/compatibility"
	"fidl/fuchsia/io"
	"fidl/fuchsia/sys"
)

var _ compatibility.EchoWithCtx = (*echoImpl)(nil)

type echoImpl struct {
	ctx *component.Context
}

func (echo *echoImpl) getServer(url string) (*compatibility.EchoWithCtxInterface, error) {
	directoryReq, directoryWithCtxInterface, err := io.NewDirectoryWithCtxInterfaceRequest()
	if err != nil {
		return nil, err
	}
	launchInfo := sys.LaunchInfo{
		Url:              url,
		DirectoryRequest: directoryReq.Channel,
	}

	componentControllerReq, _, err := sys.NewComponentControllerWithCtxInterfaceRequest()
	if err != nil {
		return nil, err
	}
	if err := echo.ctx.Launcher().CreateComponent(context.Background(), launchInfo, componentControllerReq); err != nil {
		return nil, err
	}

	echoReq, echoWithCtxInterface, err := compatibility.NewEchoWithCtxInterfaceRequest()
	if err != nil {
		return nil, err
	}
	if err := fdio.ServiceConnectAt(zx.Handle(directoryWithCtxInterface.Channel), echoReq.Name(), zx.Handle(echoReq.Channel)); err != nil {
		return nil, err
	}
	return echoWithCtxInterface, nil
}

func (echo *echoImpl) EchoStruct(_ fidl.Context, value compatibility.Struct, forwardURL string) (compatibility.Struct, error) {
	if forwardURL == "" {
		return value, nil
	}
	echoWithCtxInterface, err := echo.getServer(forwardURL)
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
	echoWithCtxInterface, err := echo.getServer(forwardURL)
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
		echoWithCtxInterface, err := echo.getServer(forwardURL)
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
	echoWithCtxInterface, err := echo.getServer(forwardURL)
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
	echoWithCtxInterface, err := echo.getServer(forwardURL)
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
	echoWithCtxInterface, err := echo.getServer(forwardURL)
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
	echoWithCtxInterface, err := echo.getServer(forwardURL)
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
	echoWithCtxInterface, err := echo.getServer(forwardURL)
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
	echoWithCtxInterface, err := echo.getServer(forwardURL)
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
	echoWithCtxInterface, err := echo.getServer(forwardURL)
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
	echoWithCtxInterface, err := echo.getServer(forwardURL)
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
		func(ctx fidl.Context, c zx.Channel) error {
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
				component.ServeExclusive(ctx, &stub, c, func(err error) {
					log.Print(err)
				})
			}()
			return nil
		},
	)

	ctx.BindStartupHandle(context.Background())
}
