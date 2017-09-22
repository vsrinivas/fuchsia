// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"

	"app/context"
	"fidl/bindings"

	"syscall/zx"
	"syscall/zx/mxerror"

	"garnet/examples/fidl/services/echo"
)

type echoImpl struct{}

func (echo *echoImpl) EchoString(inValue *string) (outValue *string, err error) {
	log.Printf("server: %s\n", *inValue)
	return inValue, nil
}

type echoDelegate struct {
	stubs []*bindings.Stub
}

func (delegate *echoDelegate) Bind(r echo.Echo_Request) {
	s := r.NewStub(&echoImpl{}, bindings.GetAsyncWaiter())
	delegate.stubs = append(delegate.stubs, s)
	go func() {
		for {
			if err := s.ServeRequest(); err != nil {
				if mxerror.Status(err) != zx.ErrPeerClosed {
					log.Println(err)
				}
				break
			}
		}
	}()
}

func (delegate *echoDelegate) Quit() {
	for _, s := range delegate.stubs {
		s.Close()
	}
}

func main() {
	c := context.CreateFromStartupInfo()
	c.OutgoingService.AddService(&echo.Echo_ServiceBinder{&echoDelegate{}})
	c.Serve()

	select {}
}
