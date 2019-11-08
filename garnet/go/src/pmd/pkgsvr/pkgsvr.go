// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgsvr

import (
	"flag"
	"log"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"

	"app/context"

	"fuchsia.googlesource.com/pmd/pkgfs"
)

// Main starts a package server program
func Main() {
	var (
		blob = flag.String("blob", "/blob", "Path at which to store blobs")
	)

	log.SetPrefix("pkgsvr: ")
	log.SetFlags(0) // no time required
	flag.Parse()

	sysPkg := flag.Arg(0)

	blobDir, err := syscall.OpenPath(*blob, syscall.O_RDWR|syscall.O_DIRECTORY, 0777)
	if err != nil {
		log.Fatalf("pkgfs: failed to open %q: %s", *blob, err)
	}

	fs, err := pkgfs.New(blobDir.(*fdio.Directory))
	if err != nil {
		log.Fatalf("pkgfs: initialization failed: %s", err)
	}

	h := context.GetStartupHandle(context.HandleInfo{Type: context.HandleUser0, Arg: 0})
	if h == zx.HandleInvalid {
		log.Fatalf("pkgfs: mount failed, no serving handle supplied in startup arguments")
	}

	if sysPkg != "" {
		var err error

		if err = fs.SetSystemRoot(sysPkg); err != nil {
			log.Printf("system: failed to set system root from blob %q: %s", sysPkg, err)
		}
		log.Printf("system: will be served from %s", sysPkg)

		// In the case of an error, we don't signal fshost for fuchsia_start, as system won't be readable.
		if err == nil {
			if err := zx.ProcHandle.Signal(zx.SignalNone, zx.SignalUser0); err != nil {
				log.Printf("system: failed to SignalUser0 on ProcHandle, fuchsia may not start: %s", err)
			}
		}
	} else {
		log.Printf("system: no system package blob supplied")
	}

	log.Printf("pkgfs serving blobfs %s", *blob)
	if err := fs.Serve(zx.Channel(h)); err != nil {
		log.Fatalf("pkgfs: serve failed on startup handle: %s", err)
	}
}
