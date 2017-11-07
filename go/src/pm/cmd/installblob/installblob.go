// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// installblob takes a file by path or by stdin and writes it to blobstore.
package main

import (
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/merkle"
)

var (
	from       = flag.String("from", "", "Path of file to read from")
	blobstore  = flag.String("blobstore", "/blobstore", "Blobstore mount to write to")
	merkleroot = flag.String("merkleroot", "", "optional: merkle root, if known")
	size       = flag.Int64("size", -1, "Size, if known")
)

func getSize(path string) int64 {
	fi, err := os.Stat(path)
	if err != nil {
		log.Fatal(err)
	}
	return fi.Size()
}

func getMerkleroot(path string) (int64, string) {
	f, err := os.Open(path)
	if err != nil {
		log.Fatal(err)
	}
	var t merkle.Tree
	sz, err := t.ReadFrom(f)
	if err != nil {
		log.Fatal(err)
	}
	return sz, fmt.Sprintf("%x", t.Root())
}

func main() {
	flag.Parse()

	// TODO(TO-583) remove once Go on fuchsia returns to logging to stdio
	log.SetOutput(os.Stdout)

	var input io.Reader = os.Stdin
	// If *from is empty, the user might be trying to stream via stdin, in which
	// case, we need a merkleroot and size.
	if *from == "" {
		if len(*merkleroot) != 64 {
			log.Fatal("if flag -from is not given, flag -merkleroot of lenght 64 is required")
		}
		if *size < 0 {
			log.Fatal("if flag -from is not given, flag -size is required")
		}

	} else {
		if *merkleroot == "" {
			*size, *merkleroot = getMerkleroot(*from)
		}
		if *size < 0 {
			*size = getSize(*from)
		}

		var err error
		input, err = os.Open(*from)
		if err != nil {
			log.Fatal(err)
		}
	}

	output, err := os.Create(filepath.Join(*blobstore, *merkleroot))
	if err != nil {
		log.Fatal(err)
	}

	if err := output.Truncate(*size); err != nil {
		log.Fatal(err)
	}

	if _, err := io.Copy(output, input); err != nil {
		log.Fatal(err)
	}

	if err := output.Close(); err != nil {
		log.Fatal(err)
	}
}
