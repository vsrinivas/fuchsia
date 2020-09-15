// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package testsharder

import (
	"fmt"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// ValidateTests validates a list of test specs against a list of available test platforms.
func ValidateTests(specs []build.TestSpec, platforms []build.DimensionSet) error {
	var errMsgs []string
	for _, spec := range specs {
		if err := validateAgainst(spec, platforms); err != nil {
			errMsgs = append(errMsgs, err.Error())
		}
	}
	if len(errMsgs) > 0 {
		return fmt.Errorf(strings.Join(errMsgs, "\n"))
	}
	return nil
}

func validateAgainst(spec build.TestSpec, platforms []build.DimensionSet) error {
	if spec.Test.Name == "" {
		return fmt.Errorf("A test spec's test must have a non-empty name")
	}
	if spec.Test.Path == "" && spec.PackageURL == "" {
		return fmt.Errorf("A test spec must have its path or package URL set")
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

// resolvesTo gives a partial ordering on DimensionSets in which one resolves to
// another if the former's dimensions are given the latter.
func resolvesTo(this, that build.DimensionSet) bool {
	if this.DeviceType != "" && this.DeviceType != that.DeviceType {
		return false
	}
	if this.OS != "" && this.OS != that.OS {
		return false
	}
	if this.Testbed != "" && this.Testbed != that.Testbed {
		return false
	}
	if this.Pool != "" && this.Pool != that.Pool {
		return false
	}
	return true
}
