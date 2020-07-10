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

	// OS is the operating system in which this test must be executed. If not
	// present, this multiplier will match tests from any operating system.
	OS string `json:"os,omitempty"`

	// TotalRuns is the number of times to run the test. If not present,
	// testsharder will try to produce exactly one full shard for this test
	// using historical test duration data.
	TotalRuns int `json:"total_runs,omitempty"`

	// Affected specifies whether the test is an affected test. If affected,
	// it will be run in a separate shard than the unaffected tests.
	Affected bool `json:"affected,omitempty"`

	// MaxAttempts is the max number of times to run this test if it fails.
	// This is the max attempts per run as specified by the `TotalRuns` field.
	MaxAttempts int `json:"max_attempts,omitempty"`
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
			return nil, fmt.Errorf("A test spec's target must have a non-empty name")
		}
	}
	return specs, nil
}
