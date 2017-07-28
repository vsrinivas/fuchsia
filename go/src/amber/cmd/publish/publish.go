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

	"fuchsia.googlesource.com/amber/publish"
)

var fuchsiaBuildDir = os.Getenv("FUCHSIA_BUILD_DIR")

const serverBase = "amber-files"

var (
	usage = "usage: publish (-p|-b) [-k=<keys_dir>] [-n=<name>] [-r=<repo_path>] -f=file "
	// TODO(jmatt) support publishing batches of files instead of just singles
	tufFile  = flag.Bool("p", false, "Publish a package.")
	regFile  = flag.Bool("b", false, "Publish a content blob.")
	filePath = flag.String("f", "", "Path of the file to publish")
	name     = flag.String("n", "", "Name/path used for the published file. This only applies to '-p', package files If not supplied, the relative path supplied to '-f' will be used.")
	repoPath = flag.String("r", filepath.Join(os.Getenv("FUCHSIA_BUILD_DIR"), serverBase), "Path to the TUF repository directory.")
	keySrc   = flag.String("k", fuchsiaBuildDir, "Directory containing the signing keys.")
)

func main() {
	flag.CommandLine.Usage = func() {
		fmt.Println(usage)
		flag.CommandLine.PrintDefaults()
	}
	flag.Parse()

	if *repoPath == serverBase {
		log.Fatal("Either set $FUCHSIA_BUILD_DIR or supply a path with -r.")
		return
	}

	if !*tufFile && !*regFile {
		log.Fatal("File must be published as either a regular or verified file!")
		return
	}

	if *tufFile && *regFile {
		log.Fatal("File can not be both regular and verified")
		return
	}

	if _, e := os.Stat(*filePath); e != nil {
		log.Fatal("File path must be valid")
		return
	}

	if e := os.MkdirAll(*repoPath, os.ModePerm); e != nil {
		log.Fatalf("Repository path %q does not exist and could not be created.\n",
			*repoPath)
	}

	repo, err := publish.InitRepo(*repoPath, *keySrc)
	if err != nil {
		log.Fatalf("Error initializing repo: %v\n", err)
		return
	}

	if *tufFile {
		if name == nil || len(*name) == 0 {
			name = filePath
		}
		if err = repo.AddPackageFile(*filePath, *name); err != nil {
			log.Fatalf("Problem adding signed file: %v\n", err)
		}
		if err = repo.CommitUpdates(); err != nil {
			log.Fatalf("error signing added file: %v\n", err)
		}
	} else {
		if name != nil && len(*name) > 0 {
			log.Fatal("Name is not a valid argument for content addressed files")
			return
		}
		//var filename string
		if *name, err = repo.AddContentBlob(*filePath); err != nil {
			log.Fatal("Error adding regular file: %v\n", err)
			return
		}

		if err := repo.CommitUpdates(); err != nil {
			log.Fatal("Error committing regular file: %v\n", err)
		}

		fmt.Printf("Added file as %s\n", *name)
	}
}
