// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package context

import (
	"fidl/bindings"
	"fmt"
	"svc/svcns"

	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/mxruntime"

	"fuchsia/go/component"
)

type Context struct {
	Environment     *component.ApplicationEnvironmentInterface
	OutgoingService *svcns.Namespace
	serviceRoot     zx.Handle
	Launcher        *component.ApplicationLauncherInterface
	appServices     zx.Handle
}

// TODO: define these in syscall/zx/mxruntime
const (
	HandleDirectoryRequest mxruntime.HandleType = 0x3B
	HandleAppServices      mxruntime.HandleType = 0x43
)

func getServiceRoot() zx.Handle {
	c0, c1, err := zx.NewChannel(0)
	if err != nil {
		return zx.HANDLE_INVALID
	}

	// TODO: Use "/svc" once that actually works.
	err = fdio.ServiceConnect("/svc/.", zx.Handle(c0))
	if err != nil {
		return zx.HANDLE_INVALID
	}
	return zx.Handle(c1)
}

func New(serviceRoot, serviceRequest, appServices zx.Handle) *Context {
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
		r := component.ServiceProviderInterface{
			bindings.NewChannelHandleOwner(c.appServices)}
		s := component.NewStubForServiceProvider(
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
	TakeChannel() zx.Handle
}

func (c *Context) ConnectToEnvService(r interfaceRequest) {
	c.ConnectToEnvServiceAt(r.Name(), r.TakeChannel())
}

func (c *Context) ConnectToEnvServiceAt(name string, h zx.Handle) {
	err := fdio.ServiceConnectAt(c.serviceRoot, name, h)
	if err != nil {
		panic(fmt.Sprintf("ConnectToEnvService: %v: %v", name, err))
	}
}

func CreateFromStartupInfo() *Context {
	serviceRequest := mxruntime.GetStartupHandle(
		mxruntime.HandleInfo{Type: HandleDirectoryRequest, Arg: 0})
	appServices := mxruntime.GetStartupHandle(
		mxruntime.HandleInfo{Type: HandleAppServices, Arg: 0})
	return New(getServiceRoot(), serviceRequest, appServices)
}
