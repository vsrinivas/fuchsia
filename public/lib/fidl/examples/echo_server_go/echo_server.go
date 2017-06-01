// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"

	"application/lib/app/context"
	"fidl/bindings"

	"syscall/mx"
	"syscall/mx/mxerror"

	"lib/fidl/examples/services/echo"
)

type EchoImpl struct{}

func (echo *EchoImpl) EchoString(inValue *string) (outValue *string, err error) {
	log.Printf("server: %s\n", *inValue)
	return inValue, nil
}

type EchoDelegate struct {
	ctx *context.Context
	stubs []*bindings.Stub
}

func NewEchoDelegate() *EchoDelegate {
	return &EchoDelegate{ctx: context.CreateFromStartupInfo()}
}

func (delegate *EchoDelegate) Create(request echo.Echo_Request) {
	stub := echo.NewEchoStub(request, &EchoImpl{}, bindings.GetAsyncWaiter())
	delegate.stubs = append(delegate.stubs, stub)
	go func() {
		for {
			if err := stub.ServeRequest(); err != nil {
				if mxerror.Status(err) != mx.ErrPeerClosed {
					log.Println(err)
				}
				break
			}
		}
	}()
}

func (delegate *EchoDelegate) Quit() {
	for _, stub := range delegate.stubs {
		stub.Close()
	}
}

func main() {
	delegate := NewEchoDelegate()
	delegate.ctx.OutgoingService.AddService(&echo.Echo_ServiceFactory{delegate})

	stub := delegate.ctx.OutgoingServiceStub
	for {
		if err := stub.ServeRequest(); err != nil {
			if mxerror.Status(err) != mx.ErrPeerClosed {
				log.Println(err)
			}
			break
		}
	}
}
