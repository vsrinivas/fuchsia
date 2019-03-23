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
	"log"
	"os"
	"path/filepath"
	"strings"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/pkg"
	"fuchsia.googlesource.com/pm/repo"
)

const (
	usage = `Usage: %s publish (-a|-ps) -C -f file [-r <repo_path>]

Pass any one of the mode flags (-a|-ps), and at least one file to pubish.
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
	packageSetMode := fs.Bool("ps", false, "(mode) Publish a set of packages from a manifest.")
	modeFlags := []*bool{archiveMode, packageSetMode}

	// TODO(raggi): cleanup args...
	config := &repo.Config{}
	//config.Vars(fs)
	fs.StringVar(&config.RepoDir, "r", "", "Path to the TUF repository directory.")
	verbose := fs.Bool("v", false, "Print out more informational messages.")
	verTime := fs.Bool("vt", false, "Set repo versioning based on time rather than a monotonic increment")

	filePaths := RepeatedArg{}
	fs.Var(&filePaths, "f", "Path(s) of the file(s) to publish")

	clean := fs.Bool("C", false, "\"clean\" the repository. only new publications remain.")

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

	if numModes > 1 {
		return fmt.Errorf("only one mode flag may be passed")
	}
	if numModes == 0 {
		return fmt.Errorf("at least one mode flag must be passed")
	}

	if len(filePaths) == 0 {
		return fmt.Errorf("no file path supplied")
	}

	// Make sure the the paths to publish actually exist.
	for _, k := range filePaths {
		if _, err := os.Stat(k); err != nil {
			return fmt.Errorf("file path %q is not valid: %s", k, err)
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
	if *verbose {
		fmt.Printf("initialized repo %s\n", config.RepoDir)
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

	switch {
	case *archiveMode:
		if len(filePaths) != 1 {
			return fmt.Errorf("too many file paths supplied")
		}

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
		if err := repo.AddPackage(name, bytes.NewReader(b)); err != nil {
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

	default:
		panic("unhandled mode")
	}

	return nil
}
