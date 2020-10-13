// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build"
)

const (
	fuchsia = "fuchsia"
	linux   = "linux"
	x64     = "x64"
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

// AffectedModifiers returns modifiers for tests that are in both testSpecs and
// affectedTestsPath.
// affectedTestsPath is the path to a file containing test names separated by `\n`.
// maxAttempts will be applied to any test that is not multiplied.
// Tests will be considered for multiplication only if num affected tests <= multiplyThreshold.
func AffectedModifiers(testSpecs []build.TestSpec, affectedTestsPath string, maxAttempts, multiplyThreshold int) ([]TestModifier, error) {
	affectedTestBytes, err := ioutil.ReadFile(affectedTestsPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read affectedTestsPath (%s): %w", affectedTestsPath, err)
	}
	affectedTestNames := strings.Split(string(affectedTestBytes), "\n")

	ret := []TestModifier{}
	// Names of tests to which we'll apply maxAttempts (i.e. we didn't multiply them).
	namesForMaxAttempts := []string{}
	if len(affectedTestNames) > multiplyThreshold {
		namesForMaxAttempts = affectedTestNames
	} else {
		nameToSpec := make(map[string]build.TestSpec)
		for _, ts := range testSpecs {
			nameToSpec[ts.Name] = ts
		}
		for _, name := range affectedTestNames {
			spec, found := nameToSpec[name]
			if !found {
				continue
			}
			// Only x64 Linux VMs are plentiful, don't multiply anything that would require
			// any other type of bot.
			if spec.CPU != x64 || (spec.OS != fuchsia && spec.OS != linux) {
				namesForMaxAttempts = append(namesForMaxAttempts, name)
				continue
			}
			foundBadEnv := false
			for _, env := range spec.Envs {
				// Don't multiply host+target tests because they tend to be flaky already.
				// The idea is to expose new flakiness, not pre-existing flakiness.
				if env.Dimensions.DeviceType != "" && spec.OS != fuchsia {
					foundBadEnv = true
					break
				}
				// Only x64 Linux VMs are plentiful, don't multiply anything that would require
				// any other type of bot.
				if env.Dimensions.DeviceType != "" && !strings.HasSuffix(env.Dimensions.DeviceType, "EMU") {
					foundBadEnv = true
					break
				}
			}
			if foundBadEnv {
				namesForMaxAttempts = append(namesForMaxAttempts, name)
				continue
			}
			ret = append(ret, TestModifier{Name: name, OS: spec.OS})
		}
	}
	for _, name := range namesForMaxAttempts {
		ret = append(ret, TestModifier{Name: name, TotalRuns: -1, Affected: true, MaxAttempts: maxAttempts})
	}
	return ret, nil
}
