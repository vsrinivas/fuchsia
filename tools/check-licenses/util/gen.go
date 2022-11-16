// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"encoding/json"
	"fmt"
	"os"
	"regexp"
	"strings"
)

type Gen struct {
	BuildSettings   map[string]interface{} `json:"build_settings"`
	Targets         map[string]*Target     `json:"targets"`
	FilteredTargets map[string]*Target

	re *regexp.Regexp
}

func NewGen(projectFile string) (*Gen, error) {
	// Read in the projects.json file.
	//
	// This file can be really large (554MB on my machine), so we may
	// need to investigate streaming this data if it becomes a problem.
	b, err := os.ReadFile(projectFile)
	if err != nil {
		return nil, fmt.Errorf("Failed to read project.json file [%v]: %v\n", projectFile, err)
	}

	gen := Gen{
		// Many rust_crate projects have a suffix in the label name that
		// doesn't map to a directory. We use a regular expression to
		// strip that part of the label text away. We store the regexp
		// in this GN struct so we don't have to recompile the regex on
		// each loop.
		re: regexp.MustCompile(`(.*)-v(\d+)_(\d+)_(\d+)(.*)`),
	}
	d := json.NewDecoder(strings.NewReader(string(b)))
	if err := d.Decode(&gen); err != nil {
		return nil, fmt.Errorf("Failed to decode project.json into struct object: %v", err)
	}

	toAdd := make(map[string]*Target, 0)
	for name, t := range gen.Targets {
		t.Name = name
		if err := t.Clean(gen.re); err != nil {
			return nil, fmt.Errorf("Failed to clean target %v: %v", t, err)
		}
		for _, n := range t.CleanNames {
			toAdd[n] = t
		}
	}

	for k, v := range toAdd {
		if _, ok := gen.Targets[k]; !ok {
			gen.Targets[k] = v
		}
	}

	return &gen, nil
}

// Process returns a list of paths that the rootTarget requires.
// The results may include GN labels or paths to files in the repository.
func (g *Gen) FilterTargets(rootTarget string) error {
	root := g.Targets[rootTarget]
	if root == nil {
		return fmt.Errorf("Failed to find %v target in the Gen target map", rootTarget)
	}

	toProcess := []*Target{root}
	seenTargets := make(map[string]*Target)

	for len(toProcess) > 0 {
		toProcessNext := []*Target{}
		for _, t := range toProcess {
			seenTargets[t.Name] = t
			toProcessNextCandidates := t.Deps
			for _, candidateName := range toProcessNextCandidates {
				t.Children = append(t.Children, g.Targets[candidateName])
				candidate := g.Targets[candidateName]
				if seenTarget, ok := seenTargets[candidateName]; !ok {
					toProcessNext = append(toProcessNext, candidate)
					seenTargets[candidateName] = seenTarget
				}
			}
		}
		toProcess = toProcessNext
	}

	g.FilteredTargets = seenTargets
	return nil
}
