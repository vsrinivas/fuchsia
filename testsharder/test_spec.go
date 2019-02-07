// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package testsharder

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/tools/build"
)

// OS is an operating system that a test may run in.
type OS string

// Acceptable OS constants.
const (
	Linux   OS = "linux"
	Fuchsia OS = "fuchsia"
	Mac     OS = "mac"
)

// TestSpec is the specification for a single test and the environments it
// should be executed in.
type TestSpec struct {
	// Test is the test that this specification is for.
	Test `json:"test"`

	// Envs is a set of environments that the test should be executed in.
	Envs []Environment `json:"environments"`
}

// Test encapsulates details about a particular test.
type Test struct {
	// Name is the full, GN source-relative target name of the test
	// (e.g., //garnet/bin/foo/tests:foo_tests).
	Name string `json:"name"`

	// (Deprecated. Use `Command` instead)
	//
	// Location is a unique reference to a test: for example, a filesystem
	// path or a Fuchsia URI.
	Location string `json:"location"`

	// OS is the operating system in which this test must be executed.
	OS OS `json:"os"`

	// Command is the command line to run to execute this test.
	Command []string `json:"command,omitempty"`

	// DepsFile is a relative path within the build directory to a file containing a JSON
	// list of the test's runtime dependencies,
	// Currently this field only makes sense for Linux and Mac tests.
	DepsFile string `json:"deps_file,omitempty"`

	// Deps is the list of paths to the test's runtime dependencies within the build
	// directory. It is read out of DepFile.
	Deps []string `json:"deps,omitempty"`
}

func (spec TestSpec) validateAgainst(platforms []DimensionSet) error {
	if spec.Test.Name == "" {
		return fmt.Errorf("A test spec's test must have a non-empty name")
	}
	if spec.Test.Location == "" {
		return fmt.Errorf("A test spec's test must have a non-empty location")
	}
	if spec.Test.OS == "" {
		return fmt.Errorf("A test spec's test must have a non-empty OS")
	}

	resolvesToOneOf := func(env Environment, platforms []DimensionSet) bool {
		for _, platform := range platforms {
			if env.Dimensions.resolvesTo(platform) {
				return true
			}
		}
		return false
	}

	var badEnvs []Environment
	for _, env := range spec.Envs {
		if !resolvesToOneOf(env, platforms) {
			badEnvs = append(badEnvs, env)
		}
	}
	if len(badEnvs) > 0 {
		return fmt.Errorf(
			`the following environments of test\n%+v were malformed
			or did not match any available test platforms:\n%+v`,
			spec.Test, badEnvs)
	}
	return nil
}

// ValidateTestSpecs validates a list of test specs against a list of test
// platform dimension sets.
func ValidateTestSpecs(specs []TestSpec, platforms []DimensionSet) error {
	errMsg := ""
	for _, spec := range specs {
		if err := spec.validateAgainst(platforms); err != nil {
			errMsg += fmt.Sprintf("\n%v", err)
		}
	}
	if errMsg != "" {
		return fmt.Errorf(errMsg)
	}
	return nil
}

// LoadTestSpecs loads a set of test specifications from a build.
func LoadTestSpecs(fuchsiaBuildDir string) ([]TestSpec, error) {
	manifestPath := filepath.Join(fuchsiaBuildDir, build.TestSpecManifestName)
	bytes, err := ioutil.ReadFile(manifestPath)
	if err != nil {
		return nil, err
	}
	var specs []TestSpec
	if err = json.Unmarshal(bytes, &specs); err != nil {
		return nil, err
	}

	for i, _ := range specs {
		if specs[i].DepsFile == "" {
			continue
		}
		path := filepath.Join(fuchsiaBuildDir, specs[i].DepsFile)
		f, err := os.Open(path)
		if err != nil {
			return nil, err
		}
		if err = json.NewDecoder(f).Decode(&specs[i].Deps); err != nil {
			return nil, err
		}
		specs[i].DepsFile = "" // No longer needed.
	}
	return specs, nil
}
