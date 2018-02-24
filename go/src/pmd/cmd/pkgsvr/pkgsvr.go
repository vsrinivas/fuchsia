// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"

	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"

	"fuchsia.googlesource.com/pmd/pkgfs"
)

var (
	sysPath   = flag.String("system", "/system", "Path at which the filesystem will be served")
	pkgfsPath = flag.String("pkgfs", "/pkgfs", "Path at which the package filesystem will be served")
	blobstore = flag.String("blobstore", "/blobstore", "Path of blobstore to use")
	index     = flag.String("index", "/data/pkgfs_index", "Path at which to store package index")
)

// bindMount opens the path at `from` and attempts to mount it's directory
// channel at `to`. This is a hack around the fact that thinfs/rpc not providing
// any features to serve multiple channels or bind directories to channels.
// XXX(raggi): this is horrendous.
func bindMount(from, to string) error {
	fromFd, err := syscall.Open(from, syscall.O_ADMIN|syscall.O_DIRECTORY, 0777)
	if err != nil {
		return err
	}
	rio, ok := syscall.FDIOForFD(fromFd).(*fdio.RemoteIO)
	if !ok {
		return fmt.Errorf("bindMount failure %q is not served by remote io protocol", from)
	}
	defer rio.Close()
	hs, err := rio.Clone()
	if err != nil {
		return fmt.Errorf("bindMount failure, rio.Clone() = %s", err)
	}
	if len(hs) > 1 {
		for _, h := range hs[1:] {
			h.Close()
		}
	}

	err = os.MkdirAll(to, os.ModePerm)
	if err != nil {
		hs[0].Close()
		return err
	}

	parentFd, err := syscall.Open(to, syscall.O_ADMIN|syscall.O_DIRECTORY, 0777)
	if err != nil {
		hs[0].Close()
		return err
	}
	defer syscall.Close(parentFd)

	if err := syscall.FDIOForFD(parentFd).IoctlSetHandle(fdio.IoctlVFSMountFS, hs[0]); err != nil {
		return fmt.Errorf("bindMount failure: %s", err)
	}

	return nil
}

func main() {
	log.SetPrefix("pkgsvr: ")
	flag.Parse()

	sysPkg := flag.Arg(0)

	// TODO(raggi): Reading from the index should be delayed until after verified boot completion
	fs, err := pkgfs.New(*index, *blobstore)
	if err != nil {
		log.Fatalf("pkgfs: initialization failed: %s", err)
	}

	if err := fs.Mount(*pkgfsPath); err != nil {
		log.Fatalf("pkgfs: mount failed: %s", err)
	}

	if sysPkg != "" {
		fs.SetSystemRoot(sysPkg)

		log.Printf("system: mounting %s at %s", sysPkg, *sysPath)

		if err := bindMount(filepath.Join(*pkgfsPath, "system"), *sysPath); err != nil {
			log.Printf("system: failed to bind mount: %s", err)
		}

		log.Printf("system: package %s mounted at %s", sysPkg, *sysPath)

		if err := zx.ProcHandle.Signal(zx.SignalNone, zx.SignalUser0); err != nil {
			log.Printf("system: failed to SignalUser0 on ProcHandle, fuchsia may not start: %s", err)
		}
	}

	log.Printf("pkgfs mounted at %s serving index %s from blobstore %s", *pkgfsPath, *index, *blobstore)

	select {}
}
