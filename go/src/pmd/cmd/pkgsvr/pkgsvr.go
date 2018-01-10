// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"path/filepath"

	"syscall/zx"

	"fuchsia.googlesource.com/pmd/pkgfs"
)

var (
	sysPath   = flag.String("system", "/system", "Path at which the filesystem will be served")
	pkgfsPath = flag.String("pkgfs", "/pkgfs", "Path at which the package filesystem will be served")
	blobstore = flag.String("blobstore", "/blobstore", "Path of blobstore to use")
	index     = flag.String("index", "/data/pkgfs_index", "Path at which to store package index")
	pkg       = flag.String("package", "", "path into blobstore for the system meta.far")
)

func mountSystem() {
	log.Printf("system: mounting %s at %s", *pkg, *sysPath)

	fs, err := pkgfs.NewSinglePackage(*pkg, *blobstore)
	if err != nil {
		log.Fatal(err)
	}

	if err := fs.Mount(*sysPath); err != nil {
		log.Fatal(err)
	}

	log.Printf("system: package %s mounted at %s", *pkg, *sysPath)

	if err := zx.ProcHandle.Signal(zx.SignalNone, zx.SignalUser0); err != nil {
		log.Printf("system: failed to SignalUser0 on ProcHandle, fuchsia may not start: %s", err)
	}
}

func main() {
	log.SetPrefix("pkgsvr: ")
	flag.Parse()

	if *pkg == "" && len(flag.Args()) == 1 {
		*pkg = filepath.Join(*blobstore, flag.Arg(0))
	}
	if *pkg != "" {
		mountSystem()
	}

	// TODO(raggi): Reading from the index should be delayed until after verified boot completion
	fs, err := pkgfs.New(*index, *blobstore)
	if err != nil {
		log.Fatalf("pkgfs: initialization failed: %s", err)
	}

	if err := fs.Mount(*pkgfsPath); err != nil {
		log.Fatalf("pkgfs: mount failed: %s", err)
	}

	log.Printf("pkgfs mounted at %s serving index %s from blobstore %s", *pkgfsPath, *index, *blobstore)

	select {}
}
