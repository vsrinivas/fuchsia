// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"regexp"
	"sort"
	"strings"
)

type Target struct {
	Name                string   `json:"name"`
	AllDependentConfigs []string `json:"all_dependent_configs"`
	Deps                []string `json:"deps"`
	Args                []string `json:"args,omitempty"`
	Inputs              []string `json:"inputs,omitempty"`
	Script              string   `json:"script,omitempty"`
	LibDirs             []string `json:"lib_dirs,omitempty"`
	Libs                []string `json:"libs,omitempty"`
	Toolchain           string   `json:"toolchain,omitempty"`

	CleanNames []string `json:"cleanNames"`
	CleanDeps  []string `json:"cleanDeps"`

	Children []*Target
}

func (t *Target) Clean(re *regexp.Regexp) error {
	paths := make([]string, 0)
	paths = append(paths, t.Deps...)
	paths = append(paths, t.Inputs...)
	paths = append(paths, t.LibDirs...)
	paths = append(paths, t.Libs...)

	var err error
	t.CleanNames, err = clean([]string{t.Name}, re)
	if err != nil {
		return err
	}

	t.CleanDeps, err = clean(paths, re)
	if err != nil {
		return err
	}
	return nil
}

// Converts a GN label string (such as those returned by Dependencies) and
// strips any target names and toolchains, thereby returning the directory
// of the label.
func clean(paths []string, re *regexp.Regexp) ([]string, error) {
	set := make(map[string]bool, 0)

	for _, path := range paths {
		thisSet := make(map[string]bool, 0)
		thisSet[path] = true

		cutColon := true
		if strings.Contains(path, "third_party/golibs") || strings.Contains(path, "rust_crates") {
			cutColon = false
		}

		// If this target isn't a rust crate target, we still want to retrieve
		// the relevant directory, not the target name in that directory.
		// If a colon exists in this string, delete it and everything after it.
		if strings.Contains(path, ":") && cutColon {
			thisSet[strings.Split(path, ":")[0]] = true
		}

		// Same goes for toolchain definitions.
		// If a parenthesis exists in this string, delete it and everything after it.
		if strings.Contains(path, "(") {
			thisSet[strings.Split(path, "(")[0]] = true
		}

		// Many rust crate libraries have a version string in their target name,
		// but no version string in their folder path. If we see this specific
		// version string pattern, remove it from the string.
		if strings.Contains(path, "{") {
			thisSet[re.ReplaceAllString(path, "-$1.$2.$3")] = true
		}

		for k := range thisSet {
			set[k] = true
		}
		// Go lib dependencies are all linked into the build system
		// using targets that are defined in the "golibs/BUILD.gn" file.
		// We want to add the actual go library subdirectory as a dependency,
		// but there is no easy way to determine that from the build target name.
		if strings.Contains(path, "third_party/golibs") {
			for k := range thisSet {
				set[strings.ReplaceAll(k, ":", "/vendor/")] = true
			}
		}

		// Rust crate dependencies are all linked into the build system
		// using targets that are defined in the "rust_crates/BUILD.gn" file.
		// We want to add the actual rust_crate subdirectory as a dependency,
		// but there is no easy way to determine that from the build target name.
		//
		// This adds all possible directories to the list. There is no harm if
		// a given directory doesn't actually exist -- check-licenses will ignore
		// those entries.
		if strings.Contains(path, "rust_crates") {
			for k := range thisSet {
				k = re.ReplaceAllString(k, "$1-$2.$3.$4")
				set[strings.ReplaceAll(k, ":", "/vendor/")] = true
				set[strings.ReplaceAll(k, ":", "/ask2patch/")] = true
				set[strings.ReplaceAll(k, ":", "/compat/")] = true
				set[strings.ReplaceAll(k, ":", "/empty/")] = true
				set[strings.ReplaceAll(k, ":", "/forks/")] = true
				set[strings.ReplaceAll(k, ":", "/mirrors/")] = true
				set[strings.ReplaceAll(k, ":", "/src/")] = true
			}
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
