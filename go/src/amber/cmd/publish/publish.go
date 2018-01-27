// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"strings"

	"amber/publish"
)

type manifestEntry struct {
	localPath  string
	remotePath string
}

var fuchsiaBuildDir = os.Getenv("FUCHSIA_BUILD_DIR")

const serverBase = "amber-files"

var (
	usage = "usage: publish (-p|-b|-m) [-k=<keys_dir>] [-n=<name>] [-r=<repo_path>] -f=file "
	// TODO(jmatt) support publishing batches of files instead of just singles
	tufFile      = flag.Bool("p", false, "Publish a package.")
	packageSet   = flag.Bool("ps", false, "Publish a set of packages from a manifest.")
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
	for _, v := range []bool{*tufFile, *packageSet, *regFile, *manifestFile} {
		if v {
			if modeCheck {
				log.Fatal("Only one mode, -p, -ps, -b, or -m may be selected")
			}
			modeCheck = true
		}
	}

	if !modeCheck {
		log.Fatal("A mode, -p, -ps, -b, or -m must be selected")
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

	if *packageSet {
		f, err := os.Open(*filePath)
		if err != nil {
			log.Fatalf("error reading package set manifest: %s", err)
		}
		defer f.Close()
		s := bufio.NewScanner(f)
		for s.Scan() {
			if err := s.Err(); err != nil {
				log.Fatalf("error reading package set manifest: %s", err)
			}

			line := s.Text()
			parts := strings.SplitN(line, "=", 2)

			if err := repo.AddPackageFile(parts[1], parts[0]); err != nil {
				log.Fatalf("Failed to add package %q from %q: %s", parts[0], parts[1], err)
			}
		}
		if err := repo.CommitUpdates(); err != nil {
			log.Fatalf("error committing repository updates: %s", err)
		}

	} else if *tufFile {
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

		fmt.Printf("Added file as %s\n", *name)
	} else {
		if err = publishManifest(*filePath, repo); err != nil {
			fmt.Printf("Error processing manifest: %s\n", err)
			os.Exit(1)
		}
	}
}

func publishManifest(manifestPath string, repo *publish.UpdateRepo) error {
	manifest, err := readManifest(manifestPath)
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
		len(manifest), len(addedBlobs), len(manifest)-len(dupes))
	return nil
}

func publishPkgUpdates(repo *publish.UpdateRepo, manifest []manifestEntry) ([]string, map[string]string, error) {
	// if we encounter an error, remove any added blobs
	addedBlobs := []string{}
	defer func() {
		for _, p := range addedBlobs {
			repo.RemoveContentBlob(p)
		}
	}()

	blobIndex := make(map[string]string)
	// read the manifest content
	for _, entry := range manifest {
		merkle, err := repo.AddContentBlob(entry.localPath)
		if err == nil {
			addedBlobs = append(addedBlobs, merkle)
		} else if !os.IsExist(err) {
			return nil, nil, fmt.Errorf("publish: error adding blob %s", err)
		}
		blobIndex[entry.localPath] = merkle
	}

	// no error so we don't want to remove any of the blobs we just added
	defer func() {
		addedBlobs = []string{}
	}()

	return addedBlobs, blobIndex, nil
}

func readManifest(manifestPath string) ([]manifestEntry, error) {
	f, err := os.Open(manifestPath)
	if err != nil {
		return nil, fmt.Errorf("publish: couldn't read manifest: %s", err)
	}

	defer f.Close()
	entries := []manifestEntry{}
	rdr := bufio.NewReader(f)

	for {
		l, err := rdr.ReadString('\n')
		if err == io.EOF {
			if len(strings.TrimSpace(l)) == 0 {
				return entries, nil
			}
			err = nil
		}

		if err != nil {
			return entries, err
		}

		l = strings.TrimSpace(l)
		parts := strings.SplitN(l, "=", 2)
		if len(parts) < 2 {
			continue
		}

		entries = append(entries,
			manifestEntry{remotePath: parts[0], localPath: parts[1]})
	}

	return entries, nil
}
