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
	// Target is the GN target name of the test.
	Target string `json:"target"`

	// OS is the operating system in which this test must be executed; treated as "fuchsia" if not present.
	OS OS `json:"os,omitempty"`

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
		if specs[i].Target == "" {
			return nil, fmt.Errorf("A test spec's target must have a non-empty name")
		}
		if specs[i].TotalRuns == 0 {
			specs[i].TotalRuns = 1
		}
		if specs[i].OS == "" {
			specs[i].OS = Fuchsia
		}
	}
	return specs, nil
}
