// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"

	"fuchsia.googlesource.com/pmd/pkgfs"
)

var (
	path      = flag.String("path", "/pkgfs", "Path at which the package filesystem will be served")
	blobstore = flag.String("blobstore", "/blobstore", "Path of blobstore to use")
	index     = flag.String("index", "/data/pkgfs_index", "Path at which to store package index")
)

func main() {
	log.SetPrefix("pmd: ")
	flag.Parse()

	fs, err := pkgfs.New(*index, *blobstore)
	if err != nil {
		log.Fatalf("pkgfs: initialization failed: %s", err)
	}

	if err := fs.Mount(*path); err != nil {
		log.Fatalf("pkgfs: mount failed: %s", err)
	}

	log.Printf("pkgfs mounted at %s serving index %s from blobstore %s", *path, *index, *blobstore)

	select {}
}
