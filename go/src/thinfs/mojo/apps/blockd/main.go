// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

//#include "mojo/public/c/system/handle.h"
//#include "mojo/public/c/system/result.h"
import "C"

import (
	"flag"
	"os"

	"interfaces/block"
	"mojo/public/go/application"
	"mojo/public/go/bindings"
	"mojo/public/go/system"

	"fuchsia.googlesource.com/thinfs/lib/block/file"
	blk "fuchsia.googlesource.com/thinfs/mojo/services/block"
	"github.com/golang/glog"
)

const defaultBlockSize = 1024

var (
	path     = flag.String("path", "", "path to backing file")
	readOnly = flag.Bool("readonly", false, "open the file read-only")
)

type delegate struct {
	dev  *blk.Device
	stub *bindings.Stub
	conn *application.Connection
}

func (d *delegate) Initialize(ctx application.Context) {
	flag.CommandLine.Parse(ctx.Args()[1:])
	if *path == "" {
		glog.Fatal("-path is required")
	}

	opts := os.O_RDWR
	caps := block.Capabilities_ReadWrite
	if *readOnly {
		opts = os.O_RDONLY
		caps = block.Capabilities_ReadOnly
	}

	f, err := os.OpenFile(*path, opts, 0666)
	if err != nil {
		glog.Fatal("unable to open file: ", err)
	}

	fdev, err := file.New(f, defaultBlockSize)
	if err != nil {
		glog.Fatal("unable to create file device: ", err)
	}

	d.dev = blk.New(fdev, block.Capabilities(caps))
}

func (d *delegate) Create(req block.Device_Request) {
	if d.stub != nil {
		glog.Fatal("Attempting to create multiple stubs")
	}

	d.stub = block.NewDeviceStub(req, d.dev, bindings.GetAsyncWaiter())
	go func() {
		// The generated bindings currently close the connection as soon as an error
		// occurs.  The error is returned from ServeRequest so we only need to serve
		// requests until a non-nil error is returned.  Until the bindings have been
		// changed to do something more sane, we can keep this loop simple.
		var err error
		for err == nil {
			err = d.stub.ServeRequest()
		}

		connErr, ok := err.(*bindings.ConnectionError)
		if !ok || !connErr.Closed() {
			// Log any error that's not a connection closed error.
			glog.Error(err)
		}
		if err := d.dev.Close(); err != nil {
			glog.Error(err)
		}
	}()
}

func (d *delegate) AcceptConnection(conn *application.Connection) {
	// Only allow the first connection.
	if d.conn != nil {
		conn.Close()
		return
	}

	d.conn = conn
	conn.ProvideServices(&block.Device_ServiceFactory{d})
}

func (d *delegate) Quit() {
	if d.stub != nil {
		d.stub.Close()
	}
}

// MojoMain is the main entry point into the application.
//export MojoMain
func MojoMain(handle C.MojoHandle) C.MojoResult {
	application.Run(&delegate{}, system.MojoHandle(handle))
	return C.MOJO_RESULT_OK
}

func main() {}
