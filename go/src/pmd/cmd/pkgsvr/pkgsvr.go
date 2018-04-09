// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"

	"syscall/zx"
	"syscall/zx/mxruntime"

	"fuchsia.googlesource.com/pmd/pkgfs"
)

var (
	blob  = flag.String("blob", "/blob", "Path at which to store blobs")
	index = flag.String("index", "/data/pkgfs_index", "Path at which to store package index")
)

func main() {
	log.SetPrefix("pkgsvr: ")
	flag.Parse()

	sysPkg := flag.Arg(0)

	// TODO(raggi): Reading from the index should be delayed until after verified boot completion
	fs, err := pkgfs.New(*index, *blob)
	if err != nil {
		log.Fatalf("pkgfs: initialization failed: %s", err)
	}

	h := mxruntime.GetStartupHandle(mxruntime.HandleInfo{Type: mxruntime.HandleUser0, Arg: 0})
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

	log.Printf("pkgfs serving index %s from blobfs %s", *index, *blob)
	if err := fs.Serve(zx.Channel(h)); err != nil {
		log.Fatalf("pkgfs: serve failed on startup handle: %s", err)
	}
}
