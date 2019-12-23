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
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/pkg"
	"fuchsia.googlesource.com/pm/repo"
)

const (
	usage = `Usage: %s publish [-a|-lp] -C -f <file> [-repo <repository directory>]
		Pass any one of the mode flags [-a|-lp], and at least one file to pubish.
`
	metaFar = "meta.far"
)

type RepeatedArg []string

func (r *RepeatedArg) Set(s string) error {
	*r = append(*r, s)
	return nil
}

func (r *RepeatedArg) String() string {
	return strings.Join(*r, " ")
}

func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("publish", flag.ExitOnError)

	archiveMode := fs.Bool("a", false, "(mode) Publish an archived package.")
	listOfPackageManifestsMode := fs.Bool("lp", false, "(mode) Publish a list of packages (and blobs) by package output manifest")
	packageSetMode := fs.Bool("ps", false, "(mode) Publish a set of packages from a manifest.")
	blobSetMode := fs.Bool("bs", false, "(mode) Publish a set of blobs from a manifest.")
	// WARNING: packageset and blobsset modes are deprecated, but are depended upon by infra code.
	modeFlags := []*bool{archiveMode, listOfPackageManifestsMode, packageSetMode, blobSetMode}

	config := &repo.Config{}
	config.Vars(fs)
	fs.StringVar(&config.RepoDir, "r", "", "(deprecated, alias for -repo).")
	verbose := fs.Bool("v", false, "Print out more informational messages.")

	filePaths := RepeatedArg{}
	fs.Var(&filePaths, "f", "Path(s) of the file(s) to publish")

	clean := fs.Bool("C", false, "\"clean\" the repository. only new publications remain.")

	depfilePath := fs.String("depfile", "", "Path to a depfile to write to")

	// NOTE(raggi): encryption as implemented is not intended to be a generally used
	// feature, as such this flag is deliberately not included in the usage line
	encryptionKey := fs.String("e", "", "Path to AES private key for blob encryption")

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return err
	}

	config.ApplyDefaults()

	var numModes int
	for _, v := range modeFlags {
		if *v {
			numModes++
		}
	}

	if numModes != 1 {
		return fmt.Errorf("exactly one mode flag must be given")
	}

	if len(filePaths) == 0 {
		return fmt.Errorf("no file path supplied")
	}

	// deps collects a list of all inputs to the publish process to be written to
	// depfilePath if requested.
	var deps []string

	// Make sure the the paths to publish actually exist.
	for _, k := range filePaths {
		if _, err := os.Stat(k); err != nil {
			return fmt.Errorf("%q: %s", k, err)
		}
	}

	// allow mkdir to fail, but check if the path exists afterward.
	os.MkdirAll(config.RepoDir, os.ModePerm)
	fi, err := os.Stat(config.RepoDir)
	if err != nil {
		return fmt.Errorf("repository path %q is not valid: %s", config.RepoDir, err)
	}

	if !fi.IsDir() {
		return fmt.Errorf("repository path %q is not a directory", config.RepoDir)
	}

	repo, err := repo.New(config.RepoDir)
	if err != nil {
		return fmt.Errorf("error initializing repo: %s", err)
	}

	if err := repo.Init(); err != nil {
		if !os.IsExist(err) {
			return err
		}
	} else {
		// If the repository was just initialized then we have more init to do
		if err := repo.GenKeys(); err != nil {
			return err
		}

		if err := repo.AddTargets([]string{}, json.RawMessage{}); err != nil {
			return err
		}

		if err := repo.CommitUpdates(config.TimeVersioned); err != nil {
			return err
		}
		if *verbose {
			fmt.Printf("initialized repo %s\n", config.RepoDir)
		}
	}

	if *clean {
		// Remove any staged items from the repository that are yet to be published.
		if err := repo.Clean(); err != nil {
			return err
		}
		// Removes all existing published targets from the repository.
		if err := repo.RemoveTargets([]string{}); err != nil {
			return err
		}
	}

	if *encryptionKey != "" {
		if err := repo.EncryptWith(*encryptionKey); err != nil {
			return err
		}
		deps = append(deps, *encryptionKey)
	}

	switch {
	case *listOfPackageManifestsMode:
		if len(filePaths) != 1 {
			return fmt.Errorf("too many file paths supplied")
		}
		deps = append(deps, filePaths[0])
		f, err := os.Open(filePaths[0])
		if err != nil {
			return err
		}
		defer f.Close()

		scanner := bufio.NewScanner(f)

		for scanner.Scan() {
			pkgManifestPath := scanner.Text()
			if *verbose {
				fmt.Printf("publishing: %s\n", pkgManifestPath)
			}
			pkgdeps, err := repo.PublishManifest(pkgManifestPath)
			if err != nil {
				return err
			}
			deps = append(deps, pkgdeps...)
		}
		if err := scanner.Err(); err != nil {
			return err
		}

		if *verbose {
			fmt.Printf("committing updates\n")
		}
		if err := repo.CommitUpdates(config.TimeVersioned); err != nil {
			log.Fatalf("error committing repository updates: %s", err)
		}
	case *archiveMode:
		if len(filePaths) != 1 {
			return fmt.Errorf("too many file paths supplied")
		}

		deps = append(deps, filePaths[0])
		f, err := os.Open(filePaths[0])
		if err != nil {
			return fmt.Errorf("open %s: %s", filePaths[0], err)
		}
		defer f.Close()

		ar, err := far.NewReader(f)
		if err != nil {
			return fmt.Errorf("open far %s: %s", f.Name(), err)
		}

		b, err := ar.ReadFile(metaFar)
		if err != nil {
			return fmt.Errorf("open %s from %s: %s", metaFar, f.Name(), err)
		}

		mf, err := far.NewReader(bytes.NewReader(b))
		if err != nil {
			return err
		}
		pb, err := mf.ReadFile("meta/package")
		if err != nil {
			return fmt.Errorf("open meta/package from %s from %s: %s", metaFar, f.Name(), err)
		}
		var p pkg.Package
		if err := json.Unmarshal(pb, &p); err != nil {
			return err
		}

		name := p.Name + "/" + p.Version

		if *verbose {
			fmt.Printf("adding package %s\n", name)
		}
		if err := repo.AddPackage(name, bytes.NewReader(b), ""); err != nil {
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
			if _, _, err := repo.AddBlob(n, bytes.NewReader(b)); err != nil {
				return err
			}
		}
		if err := repo.CommitUpdates(config.TimeVersioned); err != nil {
			log.Fatalf("error committing repository updates: %s", err)
		}

	// WARNING: the following two modes are load bearing in infra, but are
	// deprecated. They do not have any test coverage. Tread carefully.
	case *packageSetMode:
		if len(filePaths) != 1 {
			return fmt.Errorf("too many file paths supplied")
		}
		deps = append(deps, filePaths[0])
		if err := eachEntry(filePaths[0], func(name, src string) error {
			deps = append(deps, src)
			f, err := os.Open(src)
			if err != nil {
				return err
			}
			defer f.Close()
			if err := repo.AddPackage(name, f, ""); err != nil {
				return fmt.Errorf("failed to add package %q from %q: %s", name, src, err)
			}
			return nil
		}); err != nil {
			return err
		}
		if err := repo.CommitUpdates(config.TimeVersioned); err != nil {
			log.Fatalf("error committing repository updates: %s", err)
		}
	case *blobSetMode:
		if len(filePaths) != 1 {
			return fmt.Errorf("too many file paths supplied")
		}
		deps = append(deps, filePaths[0])
		if err := eachEntry(filePaths[0], func(merkle, src string) error {
			deps = append(deps, src)
			f, err := os.Open(src)
			if err != nil {
				return err
			}
			defer f.Close()
			if _, _, err := repo.AddBlob(merkle, f); err != nil {
				return fmt.Errorf("failed to add blob %q from %q: %s", merkle, src, err)
			}
			return nil
		}); err != nil {
			return err
		}

	default:
		panic("unhandled mode")
	}

	if *depfilePath != "" {
		timestampPath := filepath.Join(config.RepoDir, "repository", "timestamp.json")
		for i, str := range deps {
			// It is not clear if this is appropriate input for the depfile, which is
			// underspecified - it's a "make format file". For the most part this should
			// not affect Fuchsia builds, as we do not use "interesting" characters in
			// our file names, but if ever we did, this could cause issues. It is at
			// least better to try to quote the strings as it is more likely we may see
			// filenames or paths containing whitespace than likely less consistent cases
			// such as handling invalid or mixed encoding issues.
			deps[i] = strconv.Quote(str)
		}
		depString := strings.Join(deps, " ")
		if err := ioutil.WriteFile(*depfilePath, []byte(fmt.Sprintf("%s: %s\n", timestampPath, depString)), 0644); err != nil {
			return err
		}
	}

	return nil
}

func eachEntry(path string, cb func(dest, src string) error) error {
	f, err := os.Open(path)
	if err != nil {
		return fmt.Errorf("error reading manifest: %s", err)
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	for s.Scan() {
		if err := s.Err(); err != nil {
			return fmt.Errorf("error reading manifest: %s", err)
		}

		line := s.Text()
		parts := strings.SplitN(line, "=", 2)
		if err := cb(parts[0], parts[1]); err != nil {
			return err
		}
	}
	return nil
}
