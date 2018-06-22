// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package publish

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"strings"

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/publish"
)

const (
	usage      = "Usage: %s publish (-p|-b|-m) [-k=<keys_dir>] [-n=<name>] [-r=<repo_path>] -f=file "
	serverBase = "amber-files"
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

func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)

	tufFile := fs.Bool("p", false, "Publish a package.")
	packageSet := fs.Bool("ps", false, "Publish a set of packages from a manifest.")
	regFile := fs.Bool("b", false, "Publish a content blob.")
	blobSet := fs.Bool("bs", false, "Publish a set of blobs from a manifest.")
	manifestFile := fs.Bool("m", false, "Publish a the contents of a manifest as as content blobs.")
	name := fs.String("n", "", "Name/path used for the published file. This only applies to '-p', package files If not supplied, the relative path supplied to '-f' will be used.")
	repoDir := fs.String("r", "", "Path to the TUF repository directory.")
	keyDir := fs.String("k", "", "Directory containing the signing keys.")
	verbose := fs.Bool("v", false, "Print out more informational messages.")
	verTime := fs.Bool("vt", false, "Set repo versioning based on time rather than a monotonic increment")
	commitStaged := fs.Bool("po", false, "Publish only, commit any staged updates to the update repo.")

	filePaths := RepeatedArg{}
	fs.Var(&filePaths, "f", "Path(s) of the file(s) to publish")

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return err
	}

	if *repoDir == "" {
		if buildDir, ok := os.LookupEnv("FUCHSIA_BUILD_DIR"); ok {
			*repoDir = filepath.Join(buildDir, serverBase)
		} else {
			return fmt.Errorf("Either set $FUCHSIA_BUILD_DIR or supply a path with -r.")
		}
	}

	if *keyDir == "" {
		if buildDir, ok := os.LookupEnv("FUCHSIA_BUILD_DIR"); ok {
			*keyDir = buildDir
		} else {
			return fmt.Errorf("Either set $FUCHSIA_BUILD_DIR or supply a key directory with -k .")
		}
	}

	modeCheck := false
	for _, v := range []bool{*tufFile, *packageSet, *regFile, *blobSet, *manifestFile,
		*commitStaged} {
		if v {
			if modeCheck {
				return fmt.Errorf("Only one mode, -p, -ps, -b, -bs, or -m may be selected")
			}
			modeCheck = true
		}
	}

	if !modeCheck {
		return fmt.Errorf("A mode, -p, -ps, -b, or -m must be selected")
	}

	if !*commitStaged && len(filePaths) == 0 {
		return fmt.Errorf("No file path supplied.")
	}

	// Make sure the the paths to publish actually exist.
	for _, k := range filePaths {
		if _, err := os.Stat(k); err != nil {
			return fmt.Errorf("file path %q is not valid: %s", k, err)
		}
	}

	// allow mkdir to fail, but check if the path exists afterward.
	os.MkdirAll(*repoDir, os.ModePerm)
	fi, err := os.Stat(*repoDir)
	if err != nil {
		return fmt.Errorf("repository path %q is not valid: %s", *repoDir, err)
	}

	if !fi.IsDir() {
		return fmt.Errorf("repository path %q is not a directory", *repoDir)
	}

	// make sure the key directory exists and is actually a directory.
	fi, err = os.Stat(*keyDir)
	if err != nil {
		return fmt.Errorf("key path %q is not not valid: %s", *keyDir, err)
	}

	if !fi.IsDir() {
		return fmt.Errorf("key path %q is not a directory", *keyDir)
	}

	repo, err := publish.InitRepo(*repoDir, *keyDir)
	if err != nil {
		return fmt.Errorf("error initializing repo: %s", err)
	}

	if *packageSet {
		if len(filePaths) != 1 {
			return fmt.Errorf("too many file paths supplied.")
		}
		f, err := os.Open(filePaths[0])
		if err != nil {
			return fmt.Errorf("error reading package set manifest: %s", err)
		}
		defer f.Close()
		s := bufio.NewScanner(f)
		for s.Scan() {
			if err := s.Err(); err != nil {
				return fmt.Errorf("error reading package set manifest: %s", err)
			}

			line := s.Text()
			parts := strings.SplitN(line, "=", 2)

			if err := repo.AddPackageFile(parts[1], parts[0]); err != nil {
				return fmt.Errorf("failed to add package %q from %q: %s", parts[0], parts[1], err)
			}
		}
		if err := repo.CommitUpdates(*verTime); err != nil {
			log.Fatalf("error committing repository updates: %s", err)
		}
	} else if *blobSet {
		if len(filePaths) != 1 {
			log.Fatalf("too many file paths supplied.")
		}
		f, err := os.Open(filePaths[0])
		if err != nil {
			return fmt.Errorf("error reading package set manifest: %s", err)
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
					return fmt.Errorf("Error adding regular file: %s", err)
				}
			}
		}

	} else if *tufFile {
		if len(filePaths) != 1 {
			return fmt.Errorf("too many file paths supplied.")
		}
		if len(*name) == 0 {
			name = &filePaths[0]
		}
		if err = repo.AddPackageFile(filePaths[0], *name); err != nil {
			return fmt.Errorf("problem adding signed file: %s", err)
		}
		if err = repo.CommitUpdates(*verTime); err != nil {
			return fmt.Errorf("error signing added file: %s", err)
		}
	} else if *regFile {
		if len(*name) > 0 {
			return fmt.Errorf("name is not a valid argument for content addressed files")
		}
		for _, path := range filePaths {
			//var filename string
			if *name, err = repo.AddContentBlob(path); err != nil && err != os.ErrExist {
				return fmt.Errorf("Error adding regular file: %s %s", path, err)
			}
		}
	} else if *commitStaged {
		if err := repo.CommitUpdates(*verTime); err != nil {
			fmt.Printf("error committing updates: %s", err)
		}
	} else {
		if len(filePaths) != 1 {
			return fmt.Errorf("too many file paths supplied.")
		}
		if err = publishManifest(filePaths[0], repo, *verbose); err != nil {
			return fmt.Errorf("error processing manifest: %s", err)
		}
	}

	return nil
}

func publishManifest(manifestPath string, repo *publish.UpdateRepo, verbose bool) error {
	manifest, err := readManifest(manifestPath)
	if err != nil {
		return err
	}

	addedBlobs, blobIndex, err := publishPkgUpdates(repo, manifest)
	if err != nil {
		return err
	}

	dupes := make(map[string]string, len(blobIndex))
	if verbose {
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
