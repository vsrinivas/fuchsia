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

type RepeatedArg []string

func (r *RepeatedArg) Set(s string) error {
	*r = append(*r, s)
	return nil
}

func (r *RepeatedArg) String() string {
	return strings.Join(*r, " ")
}

type manifestEntry struct {
	localPath  string
	remotePath string
}

var fuchsiaBuildDir = os.Getenv("FUCHSIA_BUILD_DIR")

const serverBase = "amber-files"

var (
	filePaths    = RepeatedArg{}
	usage        = "usage: publish (-p|-b|-m) [-k=<keys_dir>] [-n=<name>] [-r=<repo_path>] -f=file "
	tufFile      = flag.Bool("p", false, "Publish a package.")
	packageSet   = flag.Bool("ps", false, "Publish a set of packages from a manifest.")
	regFile      = flag.Bool("b", false, "Publish a content blob.")
	blobSet      = flag.Bool("bs", false, "Publish a set of blobs from a manifest.")
	manifestFile = flag.Bool("m", false, "Publish a the contents of a manifest as as content blobs.")
	name         = flag.String("n", "", "Name/path used for the published file. This only applies to '-p', package files If not supplied, the relative path supplied to '-f' will be used.")
	repoPath     = flag.String("r", "", "Path to the TUF repository directory.")
	keySrc       = flag.String("k", fuchsiaBuildDir, "Directory containing the signing keys.")
	verbose      = flag.Bool("v", false, "Print out more informational messages.")
	verTime      = flag.Bool("vt", false, "Set repo versioning based on time rather than a monotonic increment")
)

func main() {
	flag.Var(&filePaths, "f", "Path(s) of the file(s) to publish")
	flag.CommandLine.Usage = func() {
		fmt.Println(usage)
		flag.CommandLine.PrintDefaults()
	}
	flag.Parse()

	if *repoPath == "" {
		if buildDir, ok := os.LookupEnv("FUCHSIA_BUILD_DIR"); ok {
			*repoPath = filepath.Join(buildDir, serverBase)
		} else {
			log.Fatal("Either set $FUCHSIA_BUILD_DIR or supply a path with -r.")
		}
	}

	modeCheck := false
	for _, v := range []bool{*tufFile, *packageSet, *regFile, *blobSet, *manifestFile} {
		if v {
			if modeCheck {
				log.Fatal("Only one mode, -p, -ps, -b, -bs, or -m may be selected")
			}
			modeCheck = true
		}
	}

	if !modeCheck {
		log.Fatal("A mode, -p, -ps, -b, or -m must be selected")
	}

	if len(filePaths) == 0 {
		log.Fatal("No file path supplied.")
	}
	for _, k := range filePaths {
		if _, e := os.Stat(k); e != nil {
			log.Fatalf("File path %q is not valid.\n", k)
			return
		}
	}

	// allow mkdir to fail, but check if the path exists afterward.
	os.MkdirAll(*repoPath, os.ModePerm)
	if _, err := os.Stat(*repoPath); err != nil {
		log.Fatalf("Repository path %q does not exist or could not be read.\n",
			*repoPath)
	}

	repo, err := publish.InitRepo(*repoPath, *keySrc)
	if err != nil {
		log.Fatalf("Error initializing repo: %s\n", err)
		return
	}

	if *packageSet {
		if len(filePaths) != 1 {
			log.Fatalf("Too many file paths supplied.")
		}
		f, err := os.Open(filePaths[0])
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
		if err := repo.CommitUpdates(*verTime); err != nil {
			log.Fatalf("error committing repository updates: %s", err)
		}
	} else if *blobSet {
		if len(filePaths) != 1 {
			log.Fatalf("Too many file paths supplied.")
		}
		f, err := os.Open(filePaths[0])
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

			if _, err = repo.AddContentBlobWithMerkle(parts[0], parts[1]); err != nil {
				if err != os.ErrExist {
					log.Fatalf("Error adding regular file: %s\n", err)
					return
				}
			}
		}

	} else if *tufFile {
		if len(filePaths) != 1 {
			log.Fatalf("Too many file paths supplied.")
		}
		if len(*name) == 0 {
			name = &filePaths[0]
		}
		if err = repo.AddPackageFile(filePaths[0], *name); err != nil {
			log.Fatalf("Problem adding signed file: %s\n", err)
		}
		if err = repo.CommitUpdates(*verTime); err != nil {
			log.Fatalf("error signing added file: %s\n", err)
		}
	} else if *regFile {
		if len(*name) > 0 {
			log.Fatal("Name is not a valid argument for content addressed files")
			return
		}
		for _, path := range filePaths {
			//var filename string
			if *name, err = repo.AddContentBlob(path); err != nil && err != os.ErrExist {
				log.Fatalf("Error adding regular file: %s %s\n", path, err)
				return
			}
		}
	} else {
		if len(filePaths) != 1 {
			log.Fatalf("Too many file paths supplied.")
		}
		if err = publishManifest(filePaths[0], repo); err != nil {
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
	if *verbose {
		for p, m := range blobIndex {
			fmt.Printf("File %q stored as %s\n", p, m)
			dupes[m] = p
		}

		fmt.Printf("Blobs\n  examined: %d\n  added: %d\n  duplicates: %d\n",
			len(manifest), len(addedBlobs), len(manifest)-len(dupes))
	}
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
		if err != nil && err != io.EOF {
			return entries, err
		}

		l = strings.TrimSpace(l)
		parts := strings.Split(l, "=")
		if len(parts) == 2 {
			entries = append(entries,
				manifestEntry{remotePath: parts[0], localPath: parts[1]})
		} else if len(parts) > 2 {
			fmt.Printf("Line %q had unexpected token count %d, expected 2\n", l,
				len(parts))
		}

		if err == io.EOF {
			break
		}
	}

	return entries, nil
}
