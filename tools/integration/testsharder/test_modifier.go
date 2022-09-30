// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"regexp"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	fuchsia = "fuchsia"
	linux   = "linux"
	x64     = "x64"
)

// matchModifiersToTests will return an error that unwraps to this if a multiplier's
// "name" field does not compile to a valid regex.
var errInvalidMultiplierRegex = fmt.Errorf("invalid multiplier regex")

// matchModifiersToTests will return an error that unwraps to this if a multiplier
// matches too many tests.
var errTooManyMultiplierMatches = fmt.Errorf("a multiplier cannot match more than %d tests", maxMatchesPerMultiplier)

// TestModifier is the specification for a single test and the number of
// times it should be run.
type TestModifier struct {
	// Name is the name of the test.
	Name string `json:"name"`

	// OS is the operating system in which this test must be executed. If not
	// present, this multiplier will match tests from any operating system.
	OS string `json:"os,omitempty"`

	// TotalRuns is the number of times to run the test. If zero, testsharder
	// will use historical test duration data to try to run this test along with
	// other multiplied tests as many times as it can within the max allowed
	// multiplied shards per environment. A negative value means to NOT designate
	// this test as a multiplier test and to leave the original runs as-is.
	TotalRuns int `json:"total_runs,omitempty"`

	// Affected specifies whether the test is an affected test. If affected,
	// it will be run in a separate shard than the unaffected tests.
	Affected bool `json:"affected,omitempty"`

	// MaxAttempts is the max number of times to run this test if it fails.
	// This is the max attempts per run as specified by the `TotalRuns` field.
	MaxAttempts int `json:"max_attempts,omitempty"`
}

// ModifierMatch is the calculated match of a single test in a single environment
// with the modifier that it matches. After processing all modifiers, we should
// return a ModifierMatch for each test-env combination that the modifiers apply to.
// An empty Env means it matches all environments.
type ModifierMatch struct {
	Test     string
	Env      build.Environment
	Modifier TestModifier
}

// LoadTestModifiers loads a set of test modifiers from a json manifest.
func LoadTestModifiers(ctx context.Context, testSpecs []build.TestSpec, manifestPath string) ([]ModifierMatch, error) {
	bytes, err := os.ReadFile(manifestPath)
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
	return matchModifiersToTests(ctx, testSpecs, specs)
}

// AffectedModifiers returns modifiers for tests that are in both testSpecs and
// affectedTestNames.
// maxAttempts will be applied to any test that is not multiplied.
// Tests will be considered for multiplication only if num affected tests <= multiplyThreshold.
func AffectedModifiers(testSpecs []build.TestSpec, affectedTestNames []string, maxAttempts, multiplyThreshold int) ([]ModifierMatch, error) {
	var ret []ModifierMatch
	if len(affectedTestNames) > multiplyThreshold {
		for _, name := range affectedTestNames {
			// Since we're not multiplying the tests, apply maxAttempts to them instead.
			ret = append(ret, ModifierMatch{
				Test: name,
				Modifier: TestModifier{
					Name:        name,
					TotalRuns:   -1,
					Affected:    true,
					MaxAttempts: maxAttempts,
				},
			})
		}
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
			// Only x64 Linux VMs are plentiful, don't multiply affected tests that
			// would require any other type of bot. Also, don't multiply isolated tests
			// because they are expected to be the only test running in its shard and
			// should only run once.
			if spec.CPU != x64 || (spec.OS != fuchsia && spec.OS != linux) || spec.Isolated {
				ret = append(ret, ModifierMatch{
					Test: name,
					Modifier: TestModifier{
						Name:        name,
						TotalRuns:   -1,
						Affected:    true,
						MaxAttempts: maxAttempts,
					},
				})
				continue
			}
			for _, env := range spec.Envs {
				shouldMultiply := true
				if env.Dimensions.DeviceType != "" && spec.OS != fuchsia {
					// Don't multiply host+target tests because they tend to be
					// flaky already. The idea is to expose new flakiness, not
					// pre-existing flakiness.
					shouldMultiply = false
				} else if env.Dimensions.DeviceType != "" &&
					!strings.HasSuffix(env.Dimensions.DeviceType, "EMU") {
					// Only x64 Linux VMs are plentiful, don't multiply affected
					// tests that would require any other type of bot.
					shouldMultiply = false
				}
				match := ModifierMatch{
					Test:     name,
					Env:      env,
					Modifier: TestModifier{Name: name, Affected: true},
				}
				if !shouldMultiply {
					match.Modifier.TotalRuns = -1
					match.Modifier.MaxAttempts = maxAttempts
				}
				ret = append(ret, match)
			}
		}
	}
	return ret, nil
}

// matchModifiersToTests analyzes the given modifiers against the testSpec to return
// modifiers that match tests exactly per allowed environment.
func matchModifiersToTests(ctx context.Context, testSpecs []build.TestSpec, modifiers []TestModifier) ([]ModifierMatch, error) {
	var ret []ModifierMatch
	var tooManyMatchesMultipliers []string
	for _, modifier := range modifiers {
		if modifier.Name == "*" {
			ret = append(ret, ModifierMatch{Modifier: modifier})
			continue
		}
		nameRegex, err := regexp.Compile(modifier.Name)
		var exactMatches []ModifierMatch
		var regexMatches []ModifierMatch
		numExactMatches := 0
		numRegexMatches := 0
		if err != nil {
			return nil, fmt.Errorf("%w %q: %s", errInvalidMultiplierRegex, modifier.Name, err)
		}
		for _, ts := range testSpecs {
			if nameRegex.FindString(ts.Name) == "" {
				continue
			}
			if modifier.OS != "" && modifier.OS != ts.OS {
				continue
			}

			isExactMatch := ts.Name == modifier.Name
			if len(ts.Envs) > 0 {
				if isExactMatch {
					numExactMatches += 1
				} else {
					numRegexMatches += 1
				}
			}
			for _, env := range ts.Envs {
				match := ModifierMatch{Test: ts.Name, Env: env, Modifier: modifier}
				if isExactMatch {
					exactMatches = append(exactMatches, match)
				} else {
					regexMatches = append(regexMatches, match)
				}
			}
		}
		// We'll consider partial regex matches only when we have no exact
		// matches.
		matches := exactMatches
		numMatches := numExactMatches
		if numMatches == 0 {
			matches = regexMatches
			numMatches = numRegexMatches
		}
		if numMatches > maxMatchesPerMultiplier {
			tooManyMatchesMultipliers = append(tooManyMatchesMultipliers, modifier.Name)
			logger.Errorf(ctx, "Multiplier %q matches too many tests (%d), maximum is %d",
				modifier.Name, len(matches), maxMatchesPerMultiplier)
			continue
		}
		ret = append(ret, matches...)
	}
	if len(tooManyMatchesMultipliers) > 0 {
		return nil, fmt.Errorf(
			"%d multiplier(s) match too many tests: %w",
			len(tooManyMatchesMultipliers), errTooManyMultiplierMatches,
		)
	}
	return ret, nil
}
