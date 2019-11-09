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

var _ compatibility.Echo = (*echoImpl)(nil)

type echoImpl struct {
	ctx *context.Context
}

func (echo *echoImpl) getServer(url string) (*compatibility.EchoInterface, error) {
	directoryReq, directoryInterface, err := io.NewDirectoryInterfaceRequest()
	if err != nil {
		return nil, err
	}
	launchInfo := sys.LaunchInfo{
		Url:              url,
		DirectoryRequest: directoryReq.Channel,
	}

	componentControllerReq, _, err := sys.NewComponentControllerInterfaceRequest()
	if err != nil {
		return nil, err
	}
	if err := echo.ctx.Launcher().CreateComponent(launchInfo, componentControllerReq); err != nil {
		return nil, err
	}

	echoReq, echoInterface, err := compatibility.NewEchoInterfaceRequest()
	if err != nil {
		return nil, err
	}
	if err := fdio.ServiceConnectAt(zx.Handle(directoryInterface.Channel), echoReq.Name(), zx.Handle(echoReq.Channel)); err != nil {
		return nil, err
	}
	return echoInterface, nil
}

func (echo *echoImpl) EchoStruct(value compatibility.Struct, forwardURL string) (compatibility.Struct, error) {
	if forwardURL == "" {
		return value, nil
	}
	echoInterface, err := echo.getServer(forwardURL)
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.Struct{}, err
	}
	response, err := echoInterface.EchoStruct(value, "")
	if err != nil {
		log.Printf("EchoStruct failed: %s", err)
		return compatibility.Struct{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoStructWithError(value compatibility.Struct, resultErr compatibility.DefaultEnum, forwardURL string, resultVariant compatibility.RespondWith) (compatibility.EchoEchoStructWithErrorResult, error) {
	if forwardURL == "" {
		if resultVariant == compatibility.RespondWithErr {
			return compatibility.EchoEchoStructWithErrorResultWithErr(resultErr), nil
		} else {
			return compatibility.EchoEchoStructWithErrorResultWithResponse(compatibility.EchoEchoStructWithErrorResponse{
				Value: value,
			}), nil
		}
	}
	echoInterface, err := echo.getServer(forwardURL)
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.EchoEchoStructWithErrorResult{}, err
	}
	response, err := echoInterface.EchoStructWithError(value, resultErr, "", resultVariant)
	if err != nil {
		log.Printf("EchoStruct failed: %s", err)
		return compatibility.EchoEchoStructWithErrorResult{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoStructNoRetVal(value compatibility.Struct, forwardURL string) error {
	if forwardURL != "" {
		echoInterface, err := echo.getServer(forwardURL)
		if err != nil {
			log.Printf("Connecting to %s failed: %s", forwardURL, err)
			return err
		}
		go func() {
			for {
				value, err := echoInterface.ExpectEchoEvent()
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
		echoInterface.EchoStructNoRetVal(value, "")
	} else {
		for _, key := range echoService.BindingKeys() {
			if pxy, ok := echoService.EventProxyFor(key); ok {
				pxy.EchoEvent(value)
			}
		}
	}
	return nil
}

func (echo *echoImpl) EchoArrays(value compatibility.ArraysStruct, forwardURL string) (compatibility.ArraysStruct, error) {
	if forwardURL == "" {
		return value, nil
	}
	echoInterface, err := echo.getServer(forwardURL)
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.ArraysStruct{}, err
	}
	response, err := echoInterface.EchoArrays(value, "")
	if err != nil {
		log.Printf("EchoArrays failed: %s", err)
		return compatibility.ArraysStruct{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoArraysWithError(value compatibility.ArraysStruct, resultErr compatibility.DefaultEnum, forwardURL string, resultVariant compatibility.RespondWith) (compatibility.EchoEchoArraysWithErrorResult, error) {
	if forwardURL == "" {
		if resultVariant == compatibility.RespondWithErr {
			return compatibility.EchoEchoArraysWithErrorResultWithErr(resultErr), nil
		} else {
			return compatibility.EchoEchoArraysWithErrorResultWithResponse(compatibility.EchoEchoArraysWithErrorResponse{
				Value: value,
			}), nil
		}
	}
	echoInterface, err := echo.getServer(forwardURL)
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.EchoEchoArraysWithErrorResult{}, err
	}
	response, err := echoInterface.EchoArraysWithError(value, resultErr, "", resultVariant)
	if err != nil {
		log.Printf("EchoArrays failed: %s", err)
		return compatibility.EchoEchoArraysWithErrorResult{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoVectors(value compatibility.VectorsStruct, forwardURL string) (compatibility.VectorsStruct, error) {
	if forwardURL == "" {
		return value, nil
	}
	echoInterface, err := echo.getServer(forwardURL)
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.VectorsStruct{}, err
	}
	response, err := echoInterface.EchoVectors(value, "")
	if err != nil {
		log.Printf("EchoVectors failed: %s", err)
		return compatibility.VectorsStruct{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoVectorsWithError(value compatibility.VectorsStruct, resultErr compatibility.DefaultEnum, forwardURL string, resultVariant compatibility.RespondWith) (compatibility.EchoEchoVectorsWithErrorResult, error) {
	if forwardURL == "" {
		if resultVariant == compatibility.RespondWithErr {
			return compatibility.EchoEchoVectorsWithErrorResultWithErr(resultErr), nil
		} else {
			return compatibility.EchoEchoVectorsWithErrorResultWithResponse(compatibility.EchoEchoVectorsWithErrorResponse{
				Value: value,
			}), nil
		}
	}
	echoInterface, err := echo.getServer(forwardURL)
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.EchoEchoVectorsWithErrorResult{}, err
	}
	response, err := echoInterface.EchoVectorsWithError(value, resultErr, "", resultVariant)
	if err != nil {
		log.Printf("EchoVectors failed: %s", err)
		return compatibility.EchoEchoVectorsWithErrorResult{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoTable(value compatibility.AllTypesTable, forwardURL string) (compatibility.AllTypesTable, error) {
	if forwardURL == "" {
		return value, nil
	}
	echoInterface, err := echo.getServer(forwardURL)
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.AllTypesTable{}, err
	}
	response, err := echoInterface.EchoTable(value, "")
	if err != nil {
		log.Printf("EchoTable failed: %s", err)
		return compatibility.AllTypesTable{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoTableWithError(value compatibility.AllTypesTable, resultErr compatibility.DefaultEnum, forwardURL string, resultVariant compatibility.RespondWith) (compatibility.EchoEchoTableWithErrorResult, error) {
	if forwardURL == "" {
		if resultVariant == compatibility.RespondWithErr {
			return compatibility.EchoEchoTableWithErrorResultWithErr(resultErr), nil
		} else {
			return compatibility.EchoEchoTableWithErrorResultWithResponse(compatibility.EchoEchoTableWithErrorResponse{
				Value: value,
			}), nil
		}
	}
	echoInterface, err := echo.getServer(forwardURL)
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.EchoEchoTableWithErrorResult{}, err
	}
	response, err := echoInterface.EchoTableWithError(value, resultErr, "", resultVariant)
	if err != nil {
		log.Printf("EchoTable failed: %s", err)
		return compatibility.EchoEchoTableWithErrorResult{}, err
	}
	return response, nil
}

func (echo *echoImpl) EchoXunions(value []compatibility.AllTypesXunion, forwardURL string) ([]compatibility.AllTypesXunion, error) {
	if forwardURL == "" {
		return value, nil
	}
	echoInterface, err := echo.getServer(forwardURL)
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return nil, err
	}
	response, err := echoInterface.EchoXunions(value, "")
	if err != nil {
		log.Printf("EchoXunions failed: %s", err)
		return nil, err
	}
	return response, nil

}

func (echo *echoImpl) EchoXunionsWithError(value []compatibility.AllTypesXunion, resultErr compatibility.DefaultEnum, forwardURL string, resultVariant compatibility.RespondWith) (compatibility.EchoEchoXunionsWithErrorResult, error) {
	if forwardURL == "" {
		if resultVariant == compatibility.RespondWithErr {
			return compatibility.EchoEchoXunionsWithErrorResultWithErr(resultErr), nil
		} else {
			return compatibility.EchoEchoXunionsWithErrorResultWithResponse(compatibility.EchoEchoXunionsWithErrorResponse{
				Value: value,
			}), nil
		}
	}
	echoInterface, err := echo.getServer(forwardURL)
	if err != nil {
		log.Printf("Connecting to %s failed: %s", forwardURL, err)
		return compatibility.EchoEchoXunionsWithErrorResult{}, err
	}
	response, err := echoInterface.EchoXunionsWithError(value, resultErr, "", resultVariant)
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
		&compatibility.EchoStub{Impl: &echoImpl{ctx: ctx}},
		func(s fidl.Stub, c zx.Channel) error {
			_, err := echoService.BindingSet.Add(s, c, nil)
			return err
		},
	)
	fidl.Serve()
}
