// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package context

import (
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
	serviceRoot zx.Handle
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

func (od OutDirectory) AddService(name string, stub fidl.Stub, addFn addFn) {
	od.getMapDirectory("svc")[name] = &Service{Stub: stub, AddFn: addFn}
}

type Context struct {
	connector   Connector
	environment struct {
		sync.Once
		*sys.EnvironmentInterface
	}
	launcher struct {
		sync.Once
		*sys.LauncherInterface
	}
	OutgoingService OutDirectory
}

func CreateFromStartupInfo() *Context {
	c0, c1, err := zx.NewChannel(0)
	if err != nil {
		panic(err)
	}

	// TODO(tamird): use "/svc" once it no longer causes crashes.
	if err := fdio.ServiceConnect("/svc/.", zx.Handle(c0)); err != nil {
		panic(err)
	}
	c := &Context{
		connector: Connector{
			serviceRoot: zx.Handle(c1),
		},
		OutgoingService: make(OutDirectory),
	}

	c.OutgoingService.AddDebug("goroutines", &FileWrapper{File: &goroutineFile{}})

	if directoryRequest := GetStartupHandle(HandleInfo{
		Type: HandleDirectoryRequest,
		Arg:  0,
	}); directoryRequest.IsValid() {
		if err := (&DirectoryWrapper{
			Directory: mapDirectory(c.OutgoingService),
		}).addConnection(0, 0, io.NodeInterfaceRequest{
			Channel: zx.Channel(directoryRequest),
		}); err != nil {
			panic(err)
		}
	}

	return c
}

func (c *Context) Connector() *Connector {
	return &c.connector
}

func (c *Context) Environment() *sys.EnvironmentInterface {
	c.environment.Do(func() {
		r, p, err := sys.NewEnvironmentInterfaceRequest()
		if err != nil {
			panic(err)
		}
		c.environment.EnvironmentInterface = p
		c.Connector().ConnectToEnvService(r)
	})
	return c.environment.EnvironmentInterface
}

func (c *Context) Launcher() *sys.LauncherInterface {
	c.launcher.Do(func() {
		r, p, err := sys.NewLauncherInterfaceRequest()
		if err != nil {
			panic(err)
		}
		c.launcher.LauncherInterface = p
		c.Connector().ConnectToEnvService(r)
	})
	return c.launcher.LauncherInterface
}

func (c *Context) ConnectToEnvService(r fidl.ServiceRequest) {
	c.Connector().ConnectToEnvService(r)
}

func (c *Connector) ConnectToEnvService(r fidl.ServiceRequest) {
	if err := fdio.ServiceConnectAt(c.serviceRoot, r.Name(), zx.Handle(r.ToChannel())); err != nil {
		panic(err)
	}
}
