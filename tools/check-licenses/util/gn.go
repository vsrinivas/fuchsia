// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

type Gn struct {
	gnPath string
	outDir string

	re *regexp.Regexp
}

// NewGn returns a GN object that is used to interface with the external GN
// tool. It can be used to discover the dependendcies of a GN target. The path
// to the external binary is taken from the command line argument (--gn_path).
// NewGn will return an error if gnPath is not a valid executable, or if
// --build_dir does not exist.
func NewGn(gnPath, buildDir string) (*Gn, error) {
	gn := &Gn{
		// Many rust_crate projects have a suffix in the label name that
		// doesn't map to a directory. We use a regular expression to
		// strip that part of the label text away. We store the regexp
		// in this GN struct so we don't have to recompile the regex on
		// each loop.
		re: regexp.MustCompile(`-v\d_\d+_\d+`),
	}

	path, err := exec.LookPath(gnPath)
	if err != nil {
		return nil, fmt.Errorf("Failed to find GN binary at path %v: %v", gnPath, err)
	}

	if _, err := os.Stat(buildDir); os.IsNotExist(err) {
		return nil, fmt.Errorf("out directory does not exist: %s", buildDir)
	}

	gn.gnPath = path
	gn.outDir = buildDir

	return gn, nil
}

// Return the dependencies of the given GN target. Calls out to the external GN
// executable. Saves the results to a file specified by gnFilterFile.
func (gn *Gn) Dependencies(ctx context.Context, gnFilterFile string, target string) ([]string, error) {
	return gn.getDeps(ctx, gnFilterFile, target)
}

// Return the dependencies of the given GN workspace. Calls out to external GN
// executable. Saves the results to a file specified by gnFilterFile.
func (gn *Gn) Gen(ctx context.Context, gnFilterFile string) ([]string, error) {
	return gn.getDeps(ctx, gnFilterFile, DefaultTarget)
}

func (gn *Gn) getDeps(ctx context.Context, gnFilterFile string, target string) ([]string, error) {
	projectFile := filepath.Join(gn.outDir, "project.json")

	if _, err := os.Stat(projectFile); err != nil {
		args := []string{
			"gen",
			gn.outDir,
			"--all",
			"--ide=json",
		}

		cmd := exec.CommandContext(ctx, gn.gnPath, args...)
		var output bytes.Buffer
		cmd.Stdout = &output
		cmd.Stderr = os.Stderr
		err := cmd.Run()
		if err != nil {
			return nil, err
		}
	} else {
		log.Println(" -> project.json already exists.")
	}
	log.Println(" -> " + target)

	// Read in the projects.json file.
	//
	// This file can be really large (554MB on my machine), so we may
	// need to investigate streaming this data if it becomes a problem.
	b, err := os.ReadFile(projectFile)
	if err != nil {
		return nil, fmt.Errorf("Failed to read project.json file [%v]: %v\n", projectFile, err)
	}

	gen := &Gen{
		BuildSettings: make(map[string]interface{}),
		Targets:       make(map[string]*Target),
	}

	d := json.NewDecoder(strings.NewReader(string(b)))
	if err := d.Decode(gen); err != nil {
		return nil, fmt.Errorf("Failed to decode project.json into struct object: %v", err)
	}
	for k, v := range gen.Targets {
		v.Name = k
		AllTargets[v.Name] = v
	}
	paths, err := gen.Process(target)
	if err != nil {
		return nil, fmt.Errorf("Failed to process gen output: %v", err)
	}

	return gn.cleanPaths(paths)
}

// Converts a GN label string (such as those returned by Dependencies) and
// strips any target names and toolchains, thereby returning the directory
// of the label.
func (gn *Gn) cleanPaths(paths []string) ([]string, error) {
	set := make(map[string]bool, 0)

	for _, path := range paths {
		set[path] = true

		// Rust crate dependencies are all linked into the build system
		// using targets that are defined in the "rust_crates/BUILD.gn" file.
		// We want to add the actual rust_crate subdirectory as a dependency,
		// but there is no easy way to determine that from the build target name.
		//
		// This adds all possible directories to the list. There is no harm if
		// a given directory doesn't actually exist -- check-licenses will ignore
		// those entries.
		if strings.Contains(path, "rust_crates") {
			set[strings.ReplaceAll(path, ":", "/vendor/")] = true
			set[strings.ReplaceAll(path, ":", "/ask2patch/")] = true
			set[strings.ReplaceAll(path, ":", "/compat/")] = true
			set[strings.ReplaceAll(path, ":", "/empty/")] = true
			set[strings.ReplaceAll(path, ":", "/forks/")] = true
			set[strings.ReplaceAll(path, ":", "/mirrors/")] = true
			set[strings.ReplaceAll(path, ":", "/src/")] = true
		}

		// If this target isn't a rust crate target, we still want to retrieve
		// the relevant directory, not the target name in that directory.
		// If a colon exists in this string, delete it and everything after it.
		if strings.Contains(path, ":") {
			set[strings.Split(path, ":")[0]] = true
		}

		// Same goes for toolchain definitions.
		// If a parenthesis exists in this string, delete it and everything after it.
		if strings.Contains(path, "{") {
			set[strings.Split(path, "(")[0]] = true
		}

		// Many rust crate libraries have a version string in their target name,
		// but no version string in their folder path. If we see this specific
		// version string pattern, remove it from the string.
		if strings.Contains(path, "{") {
			set[gn.re.ReplaceAllString(path, "")] = true
		}
	}

	// Sort the results, so the outputs are deterministic.
	results := make([]string, 0)
	for k := range set {
		results = append(results, k)
	}
	sort.Strings(results)

	return results, nil
}
