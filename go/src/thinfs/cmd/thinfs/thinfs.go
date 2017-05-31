// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia

package main

import (
	"errors"
	"flag"
	"fmt"
	"os"

	fileBlk "fuchsia.googlesource.com/thinfs/block/file"

	"fuchsia.googlesource.com/thinfs/fs"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs"
	"fuchsia.googlesource.com/thinfs/magenta/rpc"

	"syscall/mx/mxio/mxc"
	"syscall/mx/mxruntime"
)

var blockFDPtr = flag.Int("blockFD", -1, "File Descriptor to Block Device")
var readOnlyPtr = flag.Bool("readonly", false, "Determines if Filesystem is mounted as Read-Only")

func parseArgs() (string, error) {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage:\n")
		fmt.Fprintf(os.Stderr, "	> %s [flags] mount\n", os.Args[0])
		flag.PrintDefaults()
	}
	flag.Parse()
	if len(flag.Args()) != 1 {
		return "", errors.New("Invalid number of arguments")
	}

	switch flag.Arg(0) {
	case "mount":
		if *blockFDPtr == -1 {
			return "", errors.New("Mount required block device file descriptor")
		}
		return "mount", nil
	default:
		return "", errors.New("Invalid action")
	}
}

func main() {
	action, err := parseArgs()
	if err != nil {
		println("Error: ", err.Error())
		os.Exit(1)
	}

	switch action {
	case "mount":
		// Open the block device
		fd, err := mxc.ExtractCMXIO(*blockFDPtr)
		if err != nil {
			println("Failed to open block device: ", err.Error())
			os.Exit(1)
		}
		f := os.NewFile(fd, "Blockdev")
		// TODO(smklein): Query the block device to access the underlying block size
		dev, err := fileBlk.New(f, 512)
		if err != nil {
			println("Failed to create block device object: ", err.Error())
			os.Exit(1)
		}

		// Start the target filesystem (FAT)
		opts := fs.ReadWrite
		if *readOnlyPtr {
			opts = fs.ReadOnly
		}
		filesys, err := msdosfs.New("Thinfs FAT", dev, opts)
		if err != nil {
			println("Failed to create FAT fs: ", err.Error())
			os.Exit(1)
		}

		h := mxruntime.GetStartupHandle(mxruntime.HandleInfo{Type: mxruntime.HandleUser0, Arg: 0})
		if h == 0 {
			println("Invalid storage handle")
			os.Exit(1)
		}

		// Mount the filesystem
		vfs, err := rpc.NewServer(filesys, h)
		if err != nil {
			println("failed to mount filesystem: ", err.Error())
			os.Exit(1)
		}
		vfs.Serve()
		filesys.Close()
	default:
		println("Unsupported arg")
		os.Exit(1)
	}
}
