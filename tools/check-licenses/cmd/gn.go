// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
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

// NewGn returns a GN object that is used to interface with the external GN tool. It can be used to
// discover the dependendcies of a GN target. The path to the external binary is taken from the
// command line argument (--gn_path). NewGn will return an error if gnPath is not a valid
// executable, or if --build_dir does not exist.
func NewGn(gnPath, buildDir string) (*Gn, error) {
	gn := &Gn{
		// Many rust_crate projects have a suffix in the label name that doesn't map to a directory.
		// We use a regular expression to strip that part of the label text away.
		// We store the regexp in this GN struct so we don't have to recompile the regex on each loop.
		re: regexp.MustCompile(`-v\d_\d+_\d+`),
	}

	path, err := exec.LookPath(gnPath)
	if err != nil {
		return nil, err
	}

	if _, err := os.Stat(buildDir); os.IsNotExist(err) {
		return nil, fmt.Errorf("out directory does not exist: %s", buildDir)
	}

	gn.gnPath = path
	gn.outDir = buildDir

	return gn, nil
}

// Return the dependencies of the given GN target. Calls out to the external GN executable.
// Saves the results to a file specified by gnFilterFile.
func (gn *Gn) Dependencies(ctx context.Context, gnFilterFile string, target string) error {
	args := []string{
		"desc",
		gn.outDir,
		target,
		"deps",
		"--all",
		"--format=json",
	}

	cmd := exec.CommandContext(ctx, gn.gnPath, args...)
	var output bytes.Buffer
	cmd.Stdout = &output
	cmd.Stderr = os.Stderr
	err := cmd.Run()
	if err != nil {
		return err
	}

	result := output.String()
	result = strings.TrimSpace(result)

	var content interface{}
	if err = json.Unmarshal([]byte(result), &content); err != nil {
		return fmt.Errorf("Failed to unmarshal `gn desc` output file [%v]: %v\n", content, err)
	}

	return gn.unpack(content, gnFilterFile)
}

// Return the dependencies of the given GN workspace. Calls out to external GN executable.
// Saves the results to a file specified by gnFilterFile.
func (gn *Gn) Gen(ctx context.Context, gnFilterFile string) error {
	projectFile := filepath.Join(gn.outDir, "project.json")
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
		return err
	}

	// Read in the projects.json file.
	//
	// This file can be really large (554MB on my machine), so we may
	// need to investigate streaming this data if it becomes a problem.
	b, err := os.ReadFile(projectFile)
	if err != nil {
		return fmt.Errorf("Failed to read project.json file [%v]: %v\n", projectFile, err)
	}

	var content interface{}
	if err = json.Unmarshal(b, &content); err != nil {
		return fmt.Errorf("Failed to unmarshal project.json file [%v]: %v\n", projectFile, err)
	}

	return gn.unpack(content, gnFilterFile)
}

// Converts a GN label string (such as those returned by Dependencies) and strips any target names
// and toolchains, thereby returning the directory of the label.
func (gn *Gn) labelToDirectory(label string) []string {
	results := make([]string, 0)
	results = append(results, label)

	// Rust crate dependencies are all linked into the build system
	// using targets that are defined in the "rust_crates/BUILD.gn" file.
	// We want to add the actual rust_crate subdirectory as a dependency,
	// but there is no easy way to determine that from the build target name.
	//
	// This adds all possible directories to the list. There is no harm if
	// a given directory doesn't actually exist -- check-licenses will ignore those entries.
	if strings.Contains(label, "rust_crates") {
		results = append(results, strings.ReplaceAll(label, ":", "/vendor/"))
		results = append(results, strings.ReplaceAll(label, ":", "/ask2patch/"))
		results = append(results, strings.ReplaceAll(label, ":", "/compat/"))
		results = append(results, strings.ReplaceAll(label, ":", "/empty/"))
		results = append(results, strings.ReplaceAll(label, ":", "/forks/"))
		results = append(results, strings.ReplaceAll(label, ":", "/mirrors/"))
		results = append(results, strings.ReplaceAll(label, ":", "/src/"))
		results = append(results, strings.ReplaceAll(label, ":", "/vendor/"))
	}

	// If this target isn't a rust crate target, we still want to retrieve the relevant directory,
	// not the target name in that directory.
	// If a colon exists in this string, delete it and everything after it.
	for i := range results {
		results[i] = strings.Split(results[i], ":")[0]
	}

	// Same goes for toolchain definitions.
	// If a parenthesis exists in this string, delete it and everything after it.
	for i := range results {
		results[i] = strings.Split(results[i], "(")[0]
	}

	// Many rust crate libraries have a version string in their target name,
	// but no version string in their folder path. If we see this specific
	// version string pattern, remove it from the string.
	for i := range results {
		results[i] = gn.re.ReplaceAllString(results[i], "")
	}

	return results
}

// The output of a "gn" command is a large json file.
// We want to retrieve any and all paths from that file, so we recursively
// look at each key / value / list item etc and add it to a string slice.
// Finally, we write that slice to the provided filepath.
func (gn *Gn) unpack(content interface{}, filename string) error {
	var recurse func(interface{}) []string

	recurse = func(content interface{}) []string {
		results := make([]string, 0)
		mapContent, ok := content.(map[string]interface{})
		if ok {
			for k, v := range mapContent {
				results = append(results, k)
				results = append(results, recurse(v)...)

			}
		}

		listContent, ok := content.([]interface{})
		if ok {
			for _, v := range listContent {
				results = append(results, recurse(v)...)
			}
		}

		stringContent, ok := content.(string)
		if ok {
			results = append(results, gn.labelToDirectory(stringContent)...)
		}
		return results
	}

	results := recurse(content)

	// Dedup the entries in the string slice.
	set := make(map[string]bool, 0)
	for _, s := range results {
		set[s] = true
	}

	// Sort the results, so the outputs are deterministic.
	results = make([]string, 0)
	for k := range set {
		results = append(results, k)
	}
	sort.Strings(results)

	// Save the results to the provided filepath.
	jsonContent, err := json.MarshalIndent(results, "", "  ")
	if err != nil {
		return err
	}
	err = os.WriteFile(filename, jsonContent, 0644)
	if err != nil {
		return err
	}
	return nil
}
