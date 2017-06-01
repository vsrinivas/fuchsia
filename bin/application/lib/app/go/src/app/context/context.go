// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package context

import (
	"fmt"
	spi "application/lib/app/service_provider_impl"
	"fidl/bindings"

	"syscall/mx"
	"syscall/mx/mxio/rio"
	"syscall/mx/mxruntime"

	ae "application/services/application_environment"
	al "application/services/application_launcher"
	sp "application/services/service_provider"
)

type Context struct {
	Environment          *ae.ApplicationEnvironment_Proxy
	OutgoingService      *spi.ServiceProviderImpl
	OutgoingServiceStub  *bindings.Stub
	serviceRoot          mx.Handle
	Launcher             *al.ApplicationLauncher_Proxy
}

// TODO: define these in syscall/mx/mxruntime
const (
	HandleAppServices     mxruntime.HandleType = 0x43
)

func getServiceRoot() mx.Handle {
	c0, c1, err := mx.NewChannel(0)
	if err != nil {
		return mx.HANDLE_INVALID
	}

	err = rio.ServiceConnect("/svc", c0.Handle)
	if err != nil {
		return mx.HANDLE_INVALID
	}
	return c1.Handle
}

func New(serviceRoot mx.Handle, servicesRequest *bindings.InterfaceRequest) *Context {
	context := &Context{serviceRoot: serviceRoot}

	request := sp.ServiceProvider_Request{
		bindings.NewChannelHandleOwner(servicesRequest.ChannelHandleOwner.PassChannel())}
	context.OutgoingService = spi.New()
	context.OutgoingServiceStub = sp.NewServiceProviderStub(request, context.OutgoingService, bindings.GetAsyncWaiter())

	envRequest, envPointer := ae.CreateChannelForApplicationEnvironment()
	context.ConnectToEnvironmentService(
		bindings.InterfaceRequest(envRequest), envRequest.Name())
	context.Environment =
		ae.NewApplicationEnvironmentProxy(envPointer, bindings.GetAsyncWaiter())

	lnchRequest, lnchPointer := al.CreateChannelForApplicationLauncher()
	context.ConnectToEnvironmentService(
		bindings.InterfaceRequest(lnchRequest), lnchRequest.Name())
	context.Launcher =
		al.NewApplicationLauncherProxy(lnchPointer, bindings.GetAsyncWaiter())

	return context
}

func (context *Context) Close() {
	// TODO: should do something here?
}

func (context *Context) ConnectToEnvironmentService(r bindings.InterfaceRequest, name string) {
	err := rio.ServiceConnectAt(context.serviceRoot, name, r.ChannelHandleOwner.PassChannel())
	if err != nil {
		panic(fmt.Sprintf("ConnectToEnvironmentService: %v", err))
	}
}

func CreateFromStartupInfo() *Context {
	services := mxruntime.GetStartupHandle(
		mxruntime.HandleInfo{Type: HandleAppServices, Arg: 0})
	return New(getServiceRoot(),
		&bindings.InterfaceRequest{
			bindings.NewChannelHandleOwner(services)})
}
