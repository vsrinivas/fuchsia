// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package publish

import (
	"bufio"
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"strings"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/pkg"
	"fuchsia.googlesource.com/pm/publish"
)

const (
	usage = `Usage: %s publish (-a|-p|-ps|-b|-bs|-m) -f file [-k <keys_dir>] [-r <repo_path>]
		Pass any one of the mode flags (-a|-p|-ps|-b|-bs|-m), and at least one file to pubish.
`
	serverBase = "amber-files"
	metaFar    = "meta.far"
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

	archiveMode := fs.Bool("a", false, "(mode) Publish an archived package.")
	packageMode := fs.Bool("p", false, "(mode) Publish a package.")
	packageSetMode := fs.Bool("ps", false, "(mode) Publish a set of packages from a manifest.")
	blobMode := fs.Bool("b", false, "(mode) Publish a content blob.")
	blobSetMode := fs.Bool("bs", false, "(mode) Publish a set of blobs from a manifest.")
	manifestMode := fs.Bool("m", false, "(mode) Publish a the contents of a manifest as as content blobs.")
	commitMode := fs.Bool("po", false, "(mode) Publish only, commit any staged updates to the update repo.")
	modeFlags := []*bool{archiveMode, packageMode, packageSetMode, blobMode, blobSetMode, manifestMode, commitMode}

	name := fs.String("n", "", "Name/path used for the published file. This only applies to '-p', package files If not supplied, the relative path supplied to '-f' will be used.")
	repoDir := fs.String("r", "", "Path to the TUF repository directory.")
	keyDir := fs.String("k", "", "Directory containing the signing keys.")
	verbose := fs.Bool("v", false, "Print out more informational messages.")
	verTime := fs.Bool("vt", false, "Set repo versioning based on time rather than a monotonic increment")

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
			return fmt.Errorf("either set $FUCHSIA_BUILD_DIR or supply a path with -r")
		}
	}

	if *keyDir == "" {
		return fmt.Errorf("a keys directory is requried")
	}

	var numModes int
	for _, v := range modeFlags {
		if *v {
			numModes++
		}
	}

	if numModes > 1 {
		return fmt.Errorf("only one mode flag may be passed")
	}
	if numModes == 0 {
		return fmt.Errorf("at least one mode flag must be passed")
	}

	if !*commitMode && len(filePaths) == 0 {
		return fmt.Errorf("no file path supplied")
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

	switch {
	case *archiveMode:
		if len(filePaths) != 1 {
			return fmt.Errorf("too many file paths supplied")
		}

		f, err := os.Open(filePaths[0])
		if err != nil {
			return err
		}
		defer f.Close()

		ar, err := far.NewReader(f)
		if err != nil {
			return err
		}

		b, err := ar.ReadFile(metaFar)
		if err != nil {
			return err
		}

		if len(*name) == 0 {
			mf, err := far.NewReader(bytes.NewReader(b))
			if err != nil {
				return err
			}
			pb, err := mf.ReadFile("package")
			if err != nil {
				return err
			}
			var p pkg.Package
			if err := json.Unmarshal(pb, &p); err != nil {
				return err
			}

			s := p.Name + "/" + p.Version
			name = &s
		}

		if err := repo.AddPackage(*name, bytes.NewReader(b)); err != nil {
			return err
		}

		for _, n := range ar.List() {
			if len(n) != 64 {
				continue
			}
			b, err := ar.ReadFile(n)
			if err != nil {
				return err
			}
			if _, err := repo.AddBlob(n, bytes.NewReader(b)); err != nil {
				return err
			}
		}
		if err := repo.CommitUpdates(*verTime); err != nil {
			log.Fatalf("error committing repository updates: %s", err)
		}

	case *packageSetMode:
		if len(filePaths) != 1 {
			return fmt.Errorf("too many file paths supplied")
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
			name := parts[0]
			f, err := os.Open(parts[1])
			if err != nil {
				return err
			}
			if err := repo.AddPackage(name, f); err != nil {
				f.Close()
				return fmt.Errorf("failed to add package %q from %q: %s", parts[0], parts[1], err)
			}
			f.Close()
		}
		if err := repo.CommitUpdates(*verTime); err != nil {
			log.Fatalf("error committing repository updates: %s", err)
		}

	case *blobSetMode:
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
			root := parts[1]
			f, err := os.Open(parts[0])
			if err != nil {
				return err
			}
			_, err = repo.AddBlob(root, f)
			f.Close()
			if err != nil && err != os.ErrExist {
				return fmt.Errorf("Error adding regular file: %s", err)
			}
		}

	case *packageMode:
		if len(filePaths) != 1 {
			return fmt.Errorf("too many file paths supplied")
		}
		if len(*name) == 0 {
			name = &filePaths[0]
		}
		f, err := os.Open(filePaths[0])
		if err != nil {
			return err
		}
		err = repo.AddPackage(*name, f)
		f.Close()
		if err != nil {
			return fmt.Errorf("problem adding signed file: %s", err)
		}
		if err = repo.CommitUpdates(*verTime); err != nil {
			return fmt.Errorf("error signing added file: %s", err)
		}

	case *blobMode:
		if len(*name) > 0 {
			return fmt.Errorf("name is not a valid argument for content addressed files")
		}
		for _, path := range filePaths {
			f, err := os.Open(path)
			if err != nil {
				return err
			}
			*name, err = repo.AddBlob("", f)
			f.Close()
			if err != nil && err != os.ErrExist {
				return fmt.Errorf("Error adding regular file: %s %s", path, err)
			}
		}

	case *commitMode:
		if err := repo.CommitUpdates(*verTime); err != nil {
			fmt.Printf("error committing updates: %s", err)
		}

	case *manifestMode:
		if len(filePaths) != 1 {
			return fmt.Errorf("too many file paths supplied")
		}
		if err = publishManifest(filePaths[0], repo, *verbose); err != nil {
			return fmt.Errorf("error processing manifest: %s", err)
		}

	default:
		panic("unhandled mode")
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
		f, err := os.Open(entry.localPath)
		if err != nil {
			return nil, nil, fmt.Errorf("publish: error adding blob %s", err)
		}

		merkle, err := repo.AddBlob("", f)
		f.Close()

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
