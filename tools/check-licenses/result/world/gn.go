// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package world

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
)

type Gn struct {
	gnPath string
	outDir string

	re *regexp.Regexp
}

type GnDeps struct {
	Targets map[string]*Target `json:"targets"`
}

type Target struct {
	Sources []string `json:sources"`
	Inputs  []string `json:inputs"`
	Deps    []string `json:deps"`
}

// NewGn returns a GN object that is used to interface with the external GN tool. It can be used to
// discover the dependendcies of a GN target. The path to the external binary is taken from the
// config argument (config.GnPath.) NewGn will return an error if config.GnPath is not a valid
// executable, or if config.BuildDir does not exist.
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
// Dependencies are returned as a GnDeps struct, which holds a series of string slices for
// "sources", "deps" and "inputs", where each entry is a GN label strings, e.g. //root/dir:target_name(toolchain)
// Both the target name and toolchain are optional. See
// https://gn.googlesource.com/gn/+/HEAD/docs/reference.md#labels for more information.
func (gn *Gn) Dependencies(ctx context.Context) (*GnDeps, error) {
	args := []string{
		"desc",
		gn.outDir,
		Config.Target,
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
		return nil, err
	}

	result := output.String()
	result = strings.TrimSpace(result)

	gnDeps := &GnDeps{
		Targets: make(map[string]*Target, 0),
	}
	if err = json.Unmarshal([]byte(result), &gnDeps.Targets); err != nil {
		return nil, fmt.Errorf("Failed to unmarshal `gn desc` output file [%v]: %v\n", gnDeps, err)
	}
	return gnDeps, nil
}

// Return the dependencies of the given GN workspace. Calls out to external GN executable.
// `fx gn gen --ide=json`
// Dependencies are returned as a GnDeps struct, which holds a series of string slices for
// "sources", "deps" and "inputs", where each entry is a GN label strings, e.g. //root/dir:target_name(toolchain)
// Both the target name and toolchain are optional. See
// https://gn.googlesource.com/gn/+/HEAD/docs/reference.md#labels for more information.
func (gn *Gn) Gen(ctx context.Context) (*GnDeps, error) {
	projectFile := filepath.Join(gn.outDir, "project.json")
	gnDeps := &GnDeps{}
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

	result := output.String()
	result = strings.TrimSpace(result)

	// Read in the projects.json file.
	//
	// This file can be really large (554MB on my machine), so we may
	// need to investigate streaming this data if it becomes a problem.
	b, err := ioutil.ReadFile(projectFile)
	if err != nil {
		return nil, fmt.Errorf("Failed to read project.json file [%v]: %v\n", projectFile, err)
	}

	// Parse the content into a data structure we can operate on.
	if err = json.Unmarshal(b, gnDeps); err != nil {
		return nil, fmt.Errorf("Failed to unmarshal project.json file [%v]: %v\n", projectFile, err)
	}
	return gnDeps, nil
}

// Converts a GN label string (such as those returned by Dependencies) and strips any target names
// and toolchains, thereby returning the directory of the label.
func (gn *Gn) LabelToDirectory(label string) string {
	// Rust crate dependencies are all linked into the build system
	// using targets that are defined in the "rust_crates/BUILD.gn" file.
	//
	// We need to convert that target into a filepath.
	// Attempt to point to the correct filepath by converting the colon (:)
	// into "/vendor/" (since most rust_crates projects end up in that subdir).
	if strings.Contains(label, "rust_crates") {
		label = strings.ReplaceAll(label, ":", "/vendor/")
	}

	// If this target isn't a rust crate target, we still want to retrieve the relevant directory,
	// not the target name in that directory.
	// If a colon exists in this string, delete it and everything after it.
	label = strings.Split(label, ":")[0]

	// Same goes for toolchain definitions.
	// If a parenthesis exists in this string, delete it and everything after it.
	label = strings.Split(label, "(")[0]

	// Many rust crate libraries have a version string in their target name,
	// but no version string in their folder path. If we see this specific
	// version string pattern, remove it from the string.
	label = gn.re.ReplaceAllString(label, "")

	return label
}

func isTarget(str string) bool {
	return strings.HasPrefix(str, "//")
}

func isDir(str string) bool {
	_, err := os.Stat(str)
	if err == nil {
		return true
	}
	if os.IsNotExist(err) {
		return false
	}
	return false
}
