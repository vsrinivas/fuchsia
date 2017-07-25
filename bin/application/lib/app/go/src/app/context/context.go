// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package context

import (
	"application/lib/svc/svcns"
	"fidl/bindings"
	"fmt"

	"syscall/mx"
	"syscall/mx/mxio/rio"
	"syscall/mx/mxruntime"

	"application/services/application_environment"
	"application/services/application_launcher"
	"application/services/service_provider"
)

type Context struct {
	Environment          *application_environment.Proxy
	OutgoingService      *svcns.Namespace
	serviceRoot          mx.Handle
	Launcher             *application_launcher.Proxy
	appServices          mx.Handle
}

// TODO: define these in syscall/mx/mxruntime
const (
	HandleServiceRequest  mxruntime.HandleType = 0x3B
	HandleAppServices     mxruntime.HandleType = 0x43
)

func getServiceRoot() mx.Handle {
	c0, c1, err := mx.NewChannel(0)
	if err != nil {
		return mx.HANDLE_INVALID
	}

	// TODO: Use "/svc" once that actually works.
	err = rio.ServiceConnect("/svc/.", c0.Handle)
	if err != nil {
		return mx.HANDLE_INVALID
	}
	return c1.Handle
}

func New(serviceRoot, serviceRequest, appServices mx.Handle) *Context {
	c := &Context{
		serviceRoot: serviceRoot,
		appServices: appServices,
	}

	c.OutgoingService = svcns.New()

	r, p := application_environment.NewChannel()
	c.ConnectToEnvironmentService(r.Name(), bindings.InterfaceRequest(r))
	c.Environment = application_environment.NewProxy(p, bindings.GetAsyncWaiter())

	r2, p2 := application_launcher.NewChannel()
	c.ConnectToEnvironmentService(r2.Name(), bindings.InterfaceRequest(r2))
	c.Launcher = application_launcher.NewProxy(p2, bindings.GetAsyncWaiter())

	if serviceRequest.IsValid() {
		c.OutgoingService.ServeDirectory(serviceRequest)
	}

	return c
}

func (c *Context) Serve() {
	if c.appServices.IsValid() {
		r := service_provider.Request{
			bindings.NewChannelHandleOwner(c.appServices)}
		s := service_provider.NewStub(
			r, c.OutgoingService, bindings.GetAsyncWaiter())
		go func() {
			for {
				if err := s.ServeRequest(); err != nil {
					break
				}
			}
		}()
	}

	if c.OutgoingService.Dispatcher != nil {
		go c.OutgoingService.Dispatcher.Serve()
	}
}

func (c *Context) Close() {
	// TODO: should do something here?
}

func (c *Context) ConnectToEnvironmentService(name string, r bindings.InterfaceRequest) {
	err := rio.ServiceConnectAt(c.serviceRoot, name, r.PassChannel())
	if err != nil {
		panic(fmt.Sprintf("ConnectToEnvironmentService: %v", err))
	}
}

func CreateFromStartupInfo() *Context {
	serviceRequest := mxruntime.GetStartupHandle(
		mxruntime.HandleInfo{Type: HandleServiceRequest, Arg: 0})
	appServices := mxruntime.GetStartupHandle(
		mxruntime.HandleInfo{Type: HandleAppServices, Arg: 0})
	return New(getServiceRoot(), serviceRequest, appServices)
}
