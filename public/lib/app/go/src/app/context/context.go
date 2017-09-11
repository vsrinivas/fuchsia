// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package context

import (
	"application/lib/svc/svcns"
	"fidl/bindings"
	"fmt"

	"syscall/mx"
	"syscall/mx/mxio"
	"syscall/mx/mxruntime"

	"application/services/application_environment"
	"application/services/application_launcher"
	"application/services/service_provider"
)

type Context struct {
	Environment     *application_environment.ApplicationEnvironment_Proxy
	OutgoingService *svcns.Namespace
	serviceRoot     mx.Handle
	Launcher        *application_launcher.ApplicationLauncher_Proxy
	appServices     mx.Handle
}

// TODO: define these in syscall/mx/mxruntime
const (
	HandleServiceRequest mxruntime.HandleType = 0x3B
	HandleAppServices    mxruntime.HandleType = 0x43
)

func getServiceRoot() mx.Handle {
	c0, c1, err := mx.NewChannel(0)
	if err != nil {
		return mx.HANDLE_INVALID
	}

	// TODO: Use "/svc" once that actually works.
	err = mxio.ServiceConnect("/svc/.", c0.Handle)
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

	r, p := c.Environment.NewRequest(bindings.GetAsyncWaiter())
	c.Environment = p
	c.ConnectToEnvService(r)

	r2, p2 := c.Launcher.NewRequest(bindings.GetAsyncWaiter())
	c.Launcher = p2
	c.ConnectToEnvService(r2)

	if serviceRequest.IsValid() {
		c.OutgoingService.ServeDirectory(serviceRequest)
	}

	return c
}

func (c *Context) Serve() {
	if c.appServices.IsValid() {
		r := service_provider.ServiceProvider_Request{
			bindings.NewChannelHandleOwner(c.appServices)}
		s := service_provider.NewStubForServiceProvider(
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

type interfaceRequest interface {
	Name() string
	PassChannel() mx.Handle
}

func (c *Context) ConnectToEnvService(r interfaceRequest) {
	c.ConnectToEnvServiceAt(r.Name(), r.PassChannel())
}

func (c *Context) ConnectToEnvServiceAt(name string, h mx.Handle) {
	err := mxio.ServiceConnectAt(c.serviceRoot, name, h)
	if err != nil {
		panic(fmt.Sprintf("ConnectToEnvService: %v: %v", name, err))
	}
}

func CreateFromStartupInfo() *Context {
	serviceRequest := mxruntime.GetStartupHandle(
		mxruntime.HandleInfo{Type: HandleServiceRequest, Arg: 0})
	appServices := mxruntime.GetStartupHandle(
		mxruntime.HandleInfo{Type: HandleAppServices, Arg: 0})
	return New(getServiceRoot(), serviceRequest, appServices)
}
