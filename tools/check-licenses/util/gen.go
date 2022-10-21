// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"fmt"
)

const (
	DefaultTarget = "//:default"
)

type Gen struct {
	BuildSettings map[string]interface{} `json:"build_settings"`
	Targets       map[string]*Target     `json:"targets"`
}

// Process returns a list of paths that the rootTarget requires.
// The results may include GN labels or paths to files in the repository.
func (g *Gen) Process(rootTarget string) ([]string, error) {
	root := g.Targets[rootTarget]
	if root == nil {
		return nil, fmt.Errorf("Failed to find %v target in the Gen target map", rootTarget)
	}

	toProcess := []*Target{root}
	seenTargets := make(map[string]bool)
	seenPaths := make(map[string]bool)

	for len(toProcess) > 0 {
		toProcessNext := []*Target{}
		for _, t := range toProcess {
			paths, toProcessNextCandidates, err := t.Process()
			if err != nil {
				return nil, fmt.Errorf("Failed to process target %v: %v", t, err)
			}
			for _, path := range paths {
				seenPaths[path] = true
			}

			for _, candidate := range toProcessNextCandidates {
				if _, ok := seenTargets[candidate.Name]; !ok {
					toProcessNext = append(toProcessNext, candidate)
					seenTargets[candidate.Name] = true
				}
			}
		}
		toProcess = toProcessNext
	}

	seenPathsList := make([]string, 0)
	for k := range seenPaths {
		seenPathsList = append(seenPathsList, k)
	}
	return seenPathsList, nil
}
