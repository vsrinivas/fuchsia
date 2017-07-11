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

	ae "application/services/application_environment"
	al "application/services/application_launcher"
	sp "application/services/service_provider"
)

type Context struct {
	Environment          *ae.ApplicationEnvironment_Proxy
	OutgoingService      *svcns.Namespace
	serviceRoot          mx.Handle
	Launcher             *al.ApplicationLauncher_Proxy
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
	context := &Context{
		serviceRoot: serviceRoot,
		appServices: appServices,
	}

	context.OutgoingService = svcns.New()

	envRequest, envPointer := ae.CreateChannelForApplicationEnvironment()
	context.ConnectToEnvironmentService(
		envRequest.Name(), bindings.InterfaceRequest(envRequest))
	context.Environment =
		ae.NewApplicationEnvironmentProxy(envPointer, bindings.GetAsyncWaiter())

	lnchRequest, lnchPointer := al.CreateChannelForApplicationLauncher()
	context.ConnectToEnvironmentService(
		lnchRequest.Name(), bindings.InterfaceRequest(lnchRequest))
	context.Launcher =
		al.NewApplicationLauncherProxy(lnchPointer, bindings.GetAsyncWaiter())

	if serviceRequest.IsValid() {
		context.OutgoingService.ServeDirectory(serviceRequest)
	}

	return context
}

func (context *Context) Serve() {
	if context.appServices.IsValid() {
		request := sp.ServiceProvider_Request{
			bindings.NewChannelHandleOwner(context.appServices)}
		stub := sp.NewServiceProviderStub(
			request, context.OutgoingService, bindings.GetAsyncWaiter())
		go func() {
			for {
				if err := stub.ServeRequest(); err != nil {
					break
				}
			}
		}()
	}

	if context.OutgoingService.Dispatcher != nil {
		go context.OutgoingService.Dispatcher.Serve()
	}
}

func (context *Context) Close() {
	// TODO: should do something here?
}

func (context *Context) ConnectToEnvironmentService(name string, r bindings.InterfaceRequest) {
	err := rio.ServiceConnectAt(context.serviceRoot, name, r.PassChannel())
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
