// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package component

import (
	"context"
	"fmt"
	"sync"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"

	"fidl/fuchsia/io"
	"fidl/fuchsia/sys"
)

// #include "zircon/process.h"
import "C"

const (
	HandleDirectoryRequest HandleType = 0x3B
	HandleAppServices      HandleType = 0x43

	HandleUser0 HandleType = 0xF0
	HandleUser1 HandleType = 0xF1
	HandleUser2 HandleType = 0xF2
)

type HandleType uint16

type HandleInfo struct {
	Type HandleType
	Arg  uint16
}

func GetStartupHandle(info HandleInfo) zx.Handle {
	id := uint32(info.Arg)<<16 | uint32(info.Type)
	h := C.zx_take_startup_handle(C.uint32_t(id))
	return zx.Handle(h)
}

type Connector struct {
	serviceRoot *io.DirectoryWithCtxInterface
}

type OutDirectory mapDirectory

func (od OutDirectory) getMapDirectory(name string) mapDirectory {
	if dir, ok := od[name]; ok {
		if dir, ok := dir.(*DirectoryWrapper); ok {
			if dir, ok := dir.Directory.(mapDirectory); ok {
				return dir
			}
			panic(fmt.Sprintf("unexpected %s type %T", name, dir))
		}
		panic(fmt.Sprintf("unexpected %s type %T", name, dir))
	}
	dir := make(mapDirectory)
	od[name] = &DirectoryWrapper{
		Directory: dir,
	}
	return dir
}

func (od OutDirectory) AddDebug(name string, n Node) {
	od.getMapDirectory("debug")[name] = n
}

func (od OutDirectory) AddDiagnostics(name string, n Node) {
	od.getMapDirectory("diagnostics")[name] = n
}

// AddService registers a named handler in the outgoing service directory.
//
// The handler function is called serially with each incoming request matching
// the name. It must not block, and is expected to handle incoming calls on the
// request.
func (od OutDirectory) AddService(name string, addFn func(context.Context, zx.Channel) error) {
	od.getMapDirectory("svc")[name] = &Service{AddFn: addFn}
}

type Context struct {
	connector   Connector
	environment struct {
		sync.Once
		*sys.EnvironmentWithCtxInterface
	}
	launcher struct {
		sync.Once
		*sys.LauncherWithCtxInterface
	}
	// OutgoingService is the directory served on the startup directory request.
	//
	// OutgoingService is cleared when BindStartupHandle is called to prevent further mutation which
	// is not supported by this implementation, though it is permitted by fuchsia.io.Directory.
	OutgoingService OutDirectory
}

// NewContextFromStartupInfo connects to the service root directory and registers
// debug services.
func NewContextFromStartupInfo() *Context {
	r, p, err := io.NewDirectoryWithCtxInterfaceRequest()
	if err != nil {
		panic(err)
	}

	// TODO(tamird): use "/svc" once it no longer causes crashes.
	if err := fdio.ServiceConnect("/svc/.", zx.Handle(r.Channel)); err != nil {
		panic(err)
	}
	c := &Context{
		connector: Connector{
			serviceRoot: p,
		},
		OutgoingService: make(OutDirectory),
	}

	c.OutgoingService.AddDebug("pprof", &DirectoryWrapper{Directory: &pprofDirectory{}})
	c.OutgoingService.AddDebug("goroutines", &FileWrapper{File: &stackTraceFile{}})

	return c
}

// BindStartupHandle takes the startup handle if it's not already taken and
// serves the out directory with all services that have been registered on
// (*Context).OutgoingService.
//
// New services must not be added after BindStartupHandle is called.
//
// BindStartupHandle blocks until the context is canceled.
func (c *Context) BindStartupHandle(ctx fidl.Context) {
	directory := mapDirectory(c.OutgoingService)
	// Prevent further mutation.
	c.OutgoingService = nil
	if directoryRequest := GetStartupHandle(HandleInfo{
		Type: HandleDirectoryRequest,
		Arg:  0,
	}); directoryRequest.IsValid() {
		if err := (&DirectoryWrapper{
			Directory: directory,
		}).addConnection(ctx, 0, 0, io.NodeWithCtxInterfaceRequest{
			Channel: zx.Channel(directoryRequest),
		}); err != nil {
			panic(err)
		}
	}
	<-ctx.Done()
}

func (c *Context) Connector() *Connector {
	return &c.connector
}

func (c *Context) Environment() *sys.EnvironmentWithCtxInterface {
	c.environment.Do(func() {
		r, p, err := sys.NewEnvironmentWithCtxInterfaceRequest()
		if err != nil {
			panic(err)
		}
		c.environment.EnvironmentWithCtxInterface = p
		c.ConnectToEnvService(r)
	})
	return c.environment.EnvironmentWithCtxInterface
}

func (c *Context) Launcher() *sys.LauncherWithCtxInterface {
	c.launcher.Do(func() {
		r, p, err := sys.NewLauncherWithCtxInterfaceRequest()
		if err != nil {
			panic(err)
		}
		c.launcher.LauncherWithCtxInterface = p
		c.ConnectToEnvService(r)
	})
	return c.launcher.LauncherWithCtxInterface
}

func (c *Context) ConnectToEnvService(r fidl.ServiceRequest) {
	if err := c.ConnectToProtocolAtPath(r.Name(), r); err != nil {
		panic(err)
	}
}

func (c *Context) ConnectToProtocolAtPath(path string, r fidl.ServiceRequest) error {
	return c.Connector().ConnectToProtocolAtPath(path, r)
}

func (c *Connector) ConnectToProtocolAtPath(path string, r fidl.ServiceRequest) error {
	return c.serviceRoot.Open(
		context.Background(),
		io.OpenFlagsRightReadable|io.OpenFlagsRightWritable,
		0,
		path,
		io.NodeWithCtxInterfaceRequest{
			Channel: r.ToChannel(),
		},
	)
}
