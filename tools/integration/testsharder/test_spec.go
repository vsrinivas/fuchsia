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

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

func validateAgainst(spec build.TestSpec, platforms []build.DimensionSet) error {
	if spec.Test.Name == "" {
		return fmt.Errorf("A test spec's test must have a non-empty name")
	}
	if spec.Test.Label == "" {
		return fmt.Errorf("A test spec's test must have a non-empty label")
	}
	if len(spec.Command) == 0 && spec.Test.Path == "" {
		return fmt.Errorf("A test spec's test must have a non-empty path or non-empty command")
	}
	if spec.Test.OS == "" {
		return fmt.Errorf("A test spec's test must have a non-empty OS")
	}

	resolvesToOneOf := func(env build.Environment, platforms []build.DimensionSet) bool {
		for _, platform := range platforms {
			if resolvesTo(env.Dimensions, platform) {
				return true
			}
		}
		return false
	}

	var badEnvs []build.Environment
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
func ValidateTests(specs []build.TestSpec, platforms []build.DimensionSet) error {
	errMsg := ""
	for _, spec := range specs {
		if err := validateAgainst(spec, platforms); err != nil {
			errMsg += fmt.Sprintf("\n%v", err)
		}
	}
	if errMsg != "" {
		return fmt.Errorf(errMsg)
	}
	return nil
}

// TODO(fxbug.dev/37955): Delete in favour of build.ModuleContext.Tests.
// LoadTestSpecs loads a set of test specifications from a build.
func LoadTestSpecs(fuchsiaBuildDir string) ([]build.TestSpec, error) {
	manifestPath := filepath.Join(fuchsiaBuildDir, build.TestModuleName)
	bytes, err := ioutil.ReadFile(manifestPath)
	if err != nil {
		return nil, err
	}
	var specs []build.TestSpec
	if err = json.Unmarshal(bytes, &specs); err != nil {
		return nil, err
	}

	for i := range specs {
		if specs[i].RuntimeDepsFile == "" {
			continue
		}
		path := filepath.Join(fuchsiaBuildDir, specs[i].RuntimeDepsFile)
		f, err := os.Open(path)
		if err != nil {
			return nil, err
		}
		if err = json.NewDecoder(f).Decode(&specs[i].Deps); err != nil {
			return nil, err
		}
		specs[i].RuntimeDepsFile = "" // No longer needed.
	}
	return specs, nil
}
