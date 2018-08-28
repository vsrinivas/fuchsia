// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package context

import (
	"fmt"
	"svc/svcns"
	"sync"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	"syscall/zx/mxruntime"

	"fidl/fuchsia/sys"
)

type Connector struct {
	serviceRoot zx.Handle
}

type Context struct {
	connector       *Connector
	environment     *sys.EnvironmentInterface
	launcher        *sys.LauncherInterface
	servicesMu      sync.Mutex
	OutgoingService *svcns.Namespace
	appServices     zx.Handle
	serviceProvider sys.ServiceProviderService
}

// TODO: define these in syscall/zx/mxruntime
const (
	HandleDirectoryRequest mxruntime.HandleType = 0x3B
	HandleAppServices      mxruntime.HandleType = 0x43
)

func getServiceRoot() zx.Handle {
	c0, c1, err := zx.NewChannel(0)
	if err != nil {
		return zx.HandleInvalid
	}

	// TODO: Use "/svc" once that actually works.
	err = fdio.ServiceConnect("/svc/.", zx.Handle(c0))
	if err != nil {
		return zx.HandleInvalid
	}
	return zx.Handle(c1)
}

func New(serviceRoot, directoryRequest, appServices zx.Handle) *Context {
	c := &Context{
		connector: &Connector{
			serviceRoot: serviceRoot,
		},
		appServices: appServices,
	}

	c.OutgoingService = svcns.New()

	if directoryRequest.IsValid() {
		c.OutgoingService.ServeDirectory(zx.Channel(directoryRequest))
	}

	return c
}

func (c *Context) Connector() *Connector {
	return c.connector
}

func (c *Context) Environment() *sys.EnvironmentInterface {
	c.servicesMu.Lock()
	defer c.servicesMu.Unlock()
	if c.environment != nil {
		return c.environment
	}
	r, p, err := sys.NewEnvironmentInterfaceRequest()
	if err != nil {
		panic(err.Error())
	}
	c.environment = p
	c.ConnectToEnvService(r)
	return c.environment
}

func (c *Context) Launcher() *sys.LauncherInterface {
	c.servicesMu.Lock()
	defer c.servicesMu.Unlock()
	if c.launcher != nil {
		return c.launcher
	}
	r, p, err := sys.NewLauncherInterfaceRequest()
	if err != nil {
		panic(err.Error())
	}
	c.launcher = p
	c.ConnectToEnvService(r)
	return c.launcher
}

func (c *Context) Serve() {
	if c.appServices.IsValid() {
		c.serviceProvider.Add(c.OutgoingService, zx.Channel(c.appServices), nil)
	}
	go fidl.Serve()
}

func (c *Context) ConnectToEnvService(r fidl.ServiceRequest) {
	c.connector.ConnectToEnvService(r)
}

func (c *Connector) ConnectToEnvService(r fidl.ServiceRequest) {
	c.ConnectToEnvServiceAt(r.Name(), r.ToChannel())
}

func (c *Connector) ConnectToEnvServiceAt(name string, h zx.Channel) {
	err := fdio.ServiceConnectAt(c.serviceRoot, name, zx.Handle(h))
	if err != nil {
		panic(fmt.Sprintf("ConnectToEnvService: %v: %v", name, err))
	}
}

func CreateFromStartupInfo() *Context {
	directoryRequest := mxruntime.GetStartupHandle(
		mxruntime.HandleInfo{Type: HandleDirectoryRequest, Arg: 0})
	appServices := mxruntime.GetStartupHandle(
		mxruntime.HandleInfo{Type: HandleAppServices, Arg: 0})
	return New(getServiceRoot(), directoryRequest, appServices)
}
