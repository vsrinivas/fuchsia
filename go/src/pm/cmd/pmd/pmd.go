// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"time"

	"fuchsia.googlesource.com/pm/pkgfs"
)

var (
	path      = flag.String("path", "/pkgfs", "Path at which the package filesystem will be served")
	blobstore = flag.String("blobstore", "/blobstore", "Path of blobstore to use")
	index     = flag.String("index", "/data/pkgfs_index", "Path at which to store package index")
)

func main() {
	flag.Parse()

	log.Printf("pkgfs going to sleep for filesystems")

	// XXX(raggi): hack to wait for filesystems to come up
	time.Sleep(10 * time.Second)

	fs, err := pkgfs.New(*index, *blobstore)
	if err != nil {
		log.Fatalf("pkgfs: initialization failed: %s", err)
	}

	if err := fs.Mount(*path); err != nil {
		log.Fatalf("pkgfs: mount failed: %s", err)
	}

	log.Printf("pkgfs mouted at %s serving index %s from blobstore %s", *path, *index, *blobstore)

	select {}
}
