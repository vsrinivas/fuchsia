// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	"syscall/zx/io"

	"app/context"

	"fidl/fidl/test/compatibility"
	"fidl/fuchsia/sys"
)

var _ compatibility.EchoWithCtx = (*echoImpl)(nil)

type echoImpl struct {
	ctx *context.Context
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
	if err := echo.ctx.Launcher().CreateComponent(fidl.Background(), launchInfo, componentControllerReq); err != nil {
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
	response, err := echoWithCtxInterface.EchoStruct(fidl.Background(), value, "")
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
	response, err := echoWithCtxInterface.EchoStructWithError(fidl.Background(), value, resultErr, "", resultVariant)
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
				value, err := echoWithCtxInterface.ExpectEchoEvent(fidl.Background())
				if err != nil {
					log.Fatalf("ExpectEchoEvent failed: %s while communicating with %s", err, forwardURL)
					return
				}
				for _, key := range echoService.BindingKeys() {
					if pxy, ok := echoService.EventProxyFor(key); ok {
						pxy.EchoEvent(value)
					}
				}
				break
			}
		}()
		echoWithCtxInterface.EchoStructNoRetVal(fidl.Background(), value, "")
	} else {
		for _, key := range echoService.BindingKeys() {
			if pxy, ok := echoService.EventProxyFor(key); ok {
				pxy.EchoEvent(value)
			}
		}
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
	response, err := echoWithCtxInterface.EchoArrays(fidl.Background(), value, "")
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
	response, err := echoWithCtxInterface.EchoArraysWithError(fidl.Background(), value, resultErr, "", resultVariant)
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
	response, err := echoWithCtxInterface.EchoVectors(fidl.Background(), value, "")
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
	response, err := echoWithCtxInterface.EchoVectorsWithError(fidl.Background(), value, resultErr, "", resultVariant)
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
	response, err := echoWithCtxInterface.EchoTable(fidl.Background(), value, "")
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
	response, err := echoWithCtxInterface.EchoTableWithError(fidl.Background(), value, resultErr, "", resultVariant)
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
	response, err := echoWithCtxInterface.EchoXunions(fidl.Background(), value, "")
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
	response, err := echoWithCtxInterface.EchoXunionsWithError(fidl.Background(), value, resultErr, "", resultVariant)
	if err != nil {
		log.Printf("EchoXunions failed: %s", err)
		return compatibility.EchoEchoXunionsWithErrorResult{}, err
	}
	return response, nil
}

var echoService compatibility.EchoService

func main() {
	ctx := context.CreateFromStartupInfo()

	ctx.OutgoingService.AddService(
		compatibility.EchoName,
		&compatibility.EchoWithCtxStub{Impl: &echoImpl{ctx: ctx}},
		func(s fidl.Stub, c zx.Channel) error {
			_, err := echoService.BindingSet.Add(s, c, nil)
			return err
		},
	)
	fidl.Serve()
}
