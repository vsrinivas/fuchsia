// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
