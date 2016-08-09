// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

//#include "mojo/public/c/system/handle.h"
//#include "mojo/public/c/system/result.h"
import "C"
import (
	"flag"
	"fmt"
	"interfaces/block"
	"interfaces/errors"
	"interfaces/filesystem/directory"
	"interfaces/filesystem/filesystem"

	"github.com/golang/glog"

	"mojo/public/go/application"
	"mojo/public/go/bindings"
	"mojo/public/go/system"
)

func convertType(t directory.FileType) string {
	switch t {
	case directory.FileType_Unknown:
		return "Unknown"
	case directory.FileType_RegularFile:
		return "Regular File"
	case directory.FileType_Directory:
		return "Directory"
	default:
		panic(fmt.Sprint("unknown file type: ", t))
	}
}

// delegate implements application.Delegate.
type delegate struct{}

func (d *delegate) Initialize(ctx application.Context) {
	flag.CommandLine.Parse(ctx.Args()[1:])

	blockReq, blockPtr := block.CreateMessagePipeForDevice()
	ctx.ConnectToApplication("mojo:blockd").ConnectToService(&blockReq)

	fsReq, fsPtr := filesystem.CreateMessagePipeForFileSystem()
	ctx.ConnectToApplication("mojo:fs").ConnectToService(&fsReq)

	fsProxy := filesystem.NewFileSystemProxy(fsPtr, bindings.GetAsyncWaiter())
	defer fsProxy.Close_Proxy()

	rootReq, rootPtr := directory.CreateMessagePipeForDirectory()
	errcode, err := fsProxy.OpenFileSystem(blockPtr, rootReq)
	if err != nil || errcode != errors.Error_Ok {
		glog.Fatalf("Unable to open file system: err=%v, errcode=%v\n", err, errcode)
	}

	rootDir := directory.NewDirectoryProxy(rootPtr, bindings.GetAsyncWaiter())
	defer rootDir.Close_Proxy()

	entries, errcode, err := rootDir.Read()
	if err != nil || errcode != errors.Error_Ok {
		glog.Fatalf("Unable to read root directory: err=%v, errcode=%v\n", err, errcode)
	}
	if entries == nil {
		glog.Fatal("entries is nil")
	}

	for _, entry := range *entries {
		glog.Infof("Entry name=%s, type=%s\n", entry.Name, convertType(entry.Type))
	}
}

func (d *delegate) AcceptConnection(conn *application.Connection) {
	conn.Close()
}

func (d *delegate) Quit() {
}

// MojoMain implements the main function for mojo applications.
//export MojoMain
func MojoMain(handle C.MojoHandle) C.MojoResult {
	application.Run(&delegate{}, system.MojoHandle(handle))
	return C.MOJO_RESULT_OK
}

func main() {
}
