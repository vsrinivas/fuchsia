// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// pkgsvr provides a single-package filesystem mount, given a blobstore path,
// a package meta.far blob path, and a mount path.
package main

import (
	"flag"
	"log"
	"path/filepath"

	"syscall/zx"

	"fuchsia.googlesource.com/pm/pkgfs"
)

var (
	blobstore = flag.String("blobstore", "/blobstore", "Path of the blobstore root to read blobs from")
	path      = flag.String("path", "/system", "Path at which the filesystem will be served")
	pkg       = flag.String("package", "", "path into blobstore for the system meta.far")
)

func main() {
	flag.Parse()

	if *pkg == "" && len(flag.Args()) == 1 {
		*pkg = filepath.Join(*blobstore, flag.Arg(0))
	}

	log.Printf("pkgsvr mounting %s at %s", *pkg, *path)

	fs, err := pkgfs.NewSinglePackage(*pkg, *blobstore)
	if err != nil {
		log.Fatal(err)
	}

	if err := fs.Mount(*path); err != nil {
		log.Fatal(err)
	}

	log.Printf("pkgsvr mounted at %s", *path)

	if err := zx.ProcHandle.Signal(zx.SignalNone, zx.SignalUser0); err != nil {
		log.Printf("pkgsvr: failed to SignalUser0 on ProcHandle, fuchsia may not start: %s", err)
	}

	select {}
}
