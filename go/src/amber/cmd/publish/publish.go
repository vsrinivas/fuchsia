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

	"amber/publish"

	"fuchsia.googlesource.com/pm/build"
)

var fuchsiaBuildDir = os.Getenv("FUCHSIA_BUILD_DIR")

const serverBase = "amber-files"

var (
	usage = "usage: publish (-p|-b|-m) [-k=<keys_dir>] [-n=<name>] [-r=<repo_path>] -f=file "
	// TODO(jmatt) support publishing batches of files instead of just singles
	tufFile      = flag.Bool("p", false, "Publish a package.")
	regFile      = flag.Bool("b", false, "Publish a content blob.")
	manifestFile = flag.Bool("m", false, "Publish a the contents of a manifest as as content blobs.")
	filePath     = flag.String("f", "", "Path of the file to publish")
	name         = flag.String("n", "", "Name/path used for the published file. This only applies to '-p', package files If not supplied, the relative path supplied to '-f' will be used.")
	repoPath     = flag.String("r", filepath.Join(os.Getenv("FUCHSIA_BUILD_DIR"), serverBase), "Path to the TUF repository directory.")
	keySrc       = flag.String("k", fuchsiaBuildDir, "Directory containing the signing keys.")
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

	modeCheck := false
	for _, v := range []bool{*tufFile, *regFile, *manifestFile} {
		if v {
			if modeCheck {
				log.Fatal("Only one mode, -p, -b, or -m may be selected")
			}
			modeCheck = true
		}
	}

	if !modeCheck {
		log.Fatal("A mode, -p, -b, or -m must be selected")
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
		log.Fatalf("Error initializing repo: %s\n", err)
		return
	}

	if *tufFile {
		if len(*name) == 0 {
			name = filePath
		}
		if err = repo.AddPackageFile(*filePath, *name); err != nil {
			log.Fatalf("Problem adding signed file: %s\n", err)
		}
		if err = repo.CommitUpdates(); err != nil {
			log.Fatalf("error signing added file: %s\n", err)
		}
	} else if *regFile {
		if len(*name) > 0 {
			log.Fatal("Name is not a valid argument for content addressed files")
			return
		}
		//var filename string
		if *name, err = repo.AddContentBlob(*filePath); err != nil {
			log.Fatalf("Error adding regular file: %s\n", err)
			return
		}

		if err := repo.CommitUpdates(); err != nil {
			log.Fatalf("Error committing regular file: %s\n", err)
		}

		fmt.Printf("Added file as %s\n", *name)
	} else {
		if err = publishManifest(*filePath, repo); err != nil {
			fmt.Printf("Error processing manifest: %s\n", err)
		}
	}
}

func publishManifest(manifestPath string, repo *publish.UpdateRepo) error {
	manifest, err := build.NewManifest([]string{manifestPath})
	if err != nil {
		return err
	}

	addedBlobs, blobIndex, err := publishPkgUpdates(repo, manifest)
	if err != nil {
		return err
	}

	dupes := make(map[string]string, len(blobIndex))
	for p, m := range blobIndex {
		fmt.Printf("File %q stored as %s\n", p, m)
		dupes[m] = p
	}

	fmt.Printf("Blobs\n  examined: %d\n  added: %d\n  duplicates: %d\n",
		len(manifest.Content()), len(addedBlobs), len(manifest.Content())-len(dupes))
	return nil
}

func publishPkgUpdates(repo *publish.UpdateRepo, manifest *build.Manifest) ([]string, map[string]string, error) {
	// if we encounter an error, remove any added blobs
	addedBlobs := []string{}
	defer func() {
		for _, p := range addedBlobs {
			repo.RemoveContentBlob(p)
		}
	}()

	blobIndex := make(map[string]string)
	// read the manifest content
	for _, local := range manifest.Content() {
		merkle, err := repo.AddContentBlob(local)
		if err == nil {
			addedBlobs = append(addedBlobs, merkle)
		} else if !os.IsExist(err) {
			return nil, nil, fmt.Errorf("publish: error adding blob %s", err)
		}
		blobIndex[local] = merkle
	}

	// no error so we don't want to remove any of the blobs we just added
	defer func() {
		addedBlobs = []string{}
	}()

	return addedBlobs, blobIndex, nil
}
