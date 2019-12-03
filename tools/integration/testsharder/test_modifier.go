// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
)

// TestModifier is the specification for a single test and the number of
// times it should be run.
type TestModifier struct {
	// Name is the name of the test.
	Name string `json:"name"`

	// Target is the GN target name of the test.
	// TODO(olivernewman): stop accepting `target` after the documentation has
	// been updated to only mention `name`
	Target string `json:"target"`

	// OS is the operating system in which this test must be executed; treated as "fuchsia" if not present.
	OS string `json:"os,omitempty"`

	// TotalRuns is the number of times to run the test; treated as 1 if not present.
	TotalRuns int `json:"total_runs,omitempty"`
}

// LoadTestModifiers loads a set of test modifiers from a json manifest.
func LoadTestModifiers(manifestPath string) ([]TestModifier, error) {
	bytes, err := ioutil.ReadFile(manifestPath)
	if err != nil {
		return nil, err
	}
	var specs []TestModifier
	if err = json.Unmarshal(bytes, &specs); err != nil {
		return nil, err
	}

	for i := range specs {
		if specs[i].Name == "" {
			if specs[i].Target == "" {
				// TODO(olivernewman): Stop accepting target and only accept
				// name after 2019-11-19.
				return nil, fmt.Errorf("A test spec's target must have a non-empty name")
			}
			specs[i].Name = specs[i].Target
			specs[i].Target = ""
		}
		if specs[i].TotalRuns == 0 {
			specs[i].TotalRuns = 1
		}
		if specs[i].OS == "" {
			specs[i].OS = "fuchsia"
		}
	}
	return specs, nil
}
