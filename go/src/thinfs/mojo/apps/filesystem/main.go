// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

// #include "mojo/public/c/system/handle.h"
// #include "mojo/public/c/system/result.h"
import "C"

import (
	"flag"

	"interfaces/filesystem/filesystem"
	"mojo/public/go/application"
	"mojo/public/go/bindings"
	"mojo/public/go/system"

	fs "fuchsia.googlesource.com/thinfs/mojo/services/filesystem"
	"github.com/golang/glog"
)

// delegate implements application.Delegate and filesystem.FileSystem_Factory.
type delegate struct {
	stubs []*bindings.Stub
}

func (d *delegate) Initialize(ctx application.Context) {
	flag.CommandLine.Parse(ctx.Args()[1:])
}

func (d *delegate) Create(req filesystem.FileSystem_Request) {
	stub := filesystem.NewFileSystemStub(req, fs.Factory{}, bindings.GetAsyncWaiter())
	d.stubs = append(d.stubs, stub)
	go func() {
		// The generated bindings currently close the connection as soon as an error
		// occurs.  The error is returned from ServeRequest so we only need to serve
		// requests until a non-nil error is returned.  Until the bindings have been
		// changed to do something more sane, we can keep this loop simple.
		var err error
		for err == nil {
			err = stub.ServeRequest()
		}

		connErr, ok := err.(*bindings.ConnectionError)
		if !ok || !connErr.Closed() {
			// Log any error that's not a connection closed error.
			glog.Error(err)
		}
	}()
}

func (d *delegate) AcceptConnection(conn *application.Connection) {
	conn.ProvideServices(&filesystem.FileSystem_ServiceFactory{d})
}

func (d *delegate) Quit() {
	for _, stub := range d.stubs {
		stub.Close()
	}
}

// MojoMain implements the main function for mojo applications.
//export MojoMain
func MojoMain(handle C.MojoHandle) C.MojoResult {
	application.Run(&delegate{}, system.MojoHandle(handle))
	return C.MOJO_RESULT_OK
}

func main() {
}
