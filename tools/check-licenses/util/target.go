// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"fmt"
)

type Target struct {
	Name                string
	AllDependentConfigs []string `json:"all_dependent_configs"`
	Deps                []string `json:"deps"`
	Args                []string `json:"args"`
	Inputs              []string `json:"inputs"`
	Script              string   `json:"script"`
	LibDirs             []string `json:"lib_dirs"`
	Libs                []string `json:"libs"`
	Toolchain           string   `json:"toolchain"`
}

// Process returns a list of paths that this target requires (e.g. "inputs")
// and a list of Targets that this target explicitly depends on.
func (t *Target) Process() ([]string, []*Target, error) {
	paths := []string{}
	targets := []*Target{}

	// Some of these fields may not be labels or paths, but that is OK.
	// We will simply ignore strings that don't map to projects later on.
	paths = append(paths, t.Name)
	paths = append(paths, t.AllDependentConfigs...)
	paths = append(paths, t.Args...)
	paths = append(paths, t.Inputs...)
	paths = append(paths, t.Script)
	paths = append(paths, t.LibDirs...)
	paths = append(paths, t.Libs...)
	paths = append(paths, t.Toolchain)

	for _, name := range t.Deps {
		if target, ok := AllTargets[name]; !ok {
			return nil, nil, fmt.Errorf("Failed to find target [%v] in AllTargets.", name)
		} else {
			targets = append(targets, target)
		}
	}

	return paths, targets, nil
}
