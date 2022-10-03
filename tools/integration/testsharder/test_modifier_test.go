// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"testing"

	"github.com/google/go-cmp/cmp"

	"go.fuchsia.dev/fuchsia/tools/build"
)

var barTestModifier = TestModifier{
	Name:      "bar",
	TotalRuns: 2,
}

var bazTestModifier = TestModifier{
	Name: "baz_host_tests",
	OS:   linux,
}

func TestLoadTestModifiers(t *testing.T) {
	areEqual := func(a, b []ModifierMatch) bool {
		stringify := func(modifier ModifierMatch) string {
			return fmt.Sprintf("%#v", modifier)
		}
		sort := func(list []ModifierMatch) {
			sort.Slice(list[:], func(i, j int) bool {
				return stringify(list[i]) < stringify(list[j])
			})
		}
		sort(a)
		sort(b)
		return reflect.DeepEqual(a, b)
	}

	tmpDir := t.TempDir()
	initial := []TestModifier{barTestModifier, bazTestModifier}

	modifiersPath := filepath.Join(tmpDir, "test_modifiers.json")
	m, err := os.Create(modifiersPath)
	if err != nil {
		t.Fatal(err)
	}
	defer m.Close()
	if err := json.NewEncoder(m).Encode(&initial); err != nil {
		t.Fatal(err)
	}

	linuxEnv := build.Environment{Dimensions: build.DimensionSet{OS: linux, CPU: x64}}
	aemuEnv := build.Environment{Dimensions: build.DimensionSet{DeviceType: "AEMU"}}
	otherDeviceEnv := build.Environment{Dimensions: build.DimensionSet{DeviceType: "other-device"}}
	specs := []build.TestSpec{
		{Test: build.Test{Name: "baz_host_tests", OS: linux, CPU: x64},
			Envs: []build.Environment{linuxEnv},
		},
		{
			Test: build.Test{Name: "bar_tests_hti", OS: linux, CPU: x64},
			Envs: []build.Environment{aemuEnv},
		},
		{
			Test: build.Test{Name: "bar_tests", OS: fuchsia, CPU: x64},
			Envs: []build.Environment{aemuEnv, otherDeviceEnv},
		},
	}

	actual, err := LoadTestModifiers(context.Background(), specs, modifiersPath)
	if err != nil {
		t.Fatalf("failed to load test modifiers: %v", err)
	}

	makeModifierMatch := func(test string, env build.Environment, modifier TestModifier) ModifierMatch {
		return ModifierMatch{
			Test:     test,
			Env:      env,
			Modifier: modifier,
		}
	}

	bazOut := makeModifierMatch("baz_host_tests", linuxEnv, bazTestModifier)
	barHTIOut := makeModifierMatch("bar_tests_hti", aemuEnv, barTestModifier)
	barOut := makeModifierMatch("bar_tests", aemuEnv, barTestModifier)
	barOut2 := makeModifierMatch("bar_tests", otherDeviceEnv, barTestModifier)
	expected := []ModifierMatch{barHTIOut, barOut, barOut2, bazOut}

	if !areEqual(expected, actual) {
		t.Fatalf("test modifiers not properly loaded:\nexpected:\n%+v\nactual:\n%+v", expected, actual)
	}
}

func TestAffectedModifiers(t *testing.T) {
	affectedTests := []string{
		"affected-arm64", "affected-linux", "affected-mac", "affected-host+target", "affected-AEMU", "affected-other-device",
	}
	const maxAttempts = 2
	t.Run("not multiplied if over threshold", func(t *testing.T) {
		mods, err := AffectedModifiers(nil, affectedTests, maxAttempts, len(affectedTests)-1)
		if err != nil {
			t.Errorf("AffectedModifiers() returned failed: %v", err)
		}
		for _, m := range mods {
			mod := m.Modifier
			if mod.MaxAttempts != maxAttempts {
				t.Errorf("%s.MaxAttempts is %d, want %d", mod.Name, mod.MaxAttempts, maxAttempts)
			}
			if !mod.Affected {
				t.Errorf("%s.Affected is false, want true", mod.Name)
			}
			if mod.TotalRuns >= 0 {
				t.Errorf("%s.TotalRuns is %d, want -1", mod.Name, mod.TotalRuns)
			}
		}
	})

	t.Run("multiplied", func(t *testing.T) {
		specs := []build.TestSpec{
			{Test: build.Test{Name: "affected-arm64", OS: linux, CPU: "arm64"}},
			{Test: build.Test{Name: "affected-linux", OS: linux, CPU: x64}},
			{Test: build.Test{Name: "affected-mac", OS: "mac", CPU: x64}},
			{
				Test: build.Test{Name: "affected-host+target", OS: linux, CPU: x64},
				Envs: []build.Environment{{Dimensions: build.DimensionSet{DeviceType: "AEMU"}}},
			},
			{
				Test: build.Test{Name: "affected-AEMU", OS: fuchsia, CPU: x64},
				Envs: []build.Environment{{Dimensions: build.DimensionSet{DeviceType: "AEMU"}}},
			},
			{
				Test: build.Test{Name: "affected-other-device", OS: fuchsia, CPU: x64},
				Envs: []build.Environment{{Dimensions: build.DimensionSet{DeviceType: "other-device"}}},
			},
			{
				Test: build.Test{Name: "affected-AEMU-and-other-device", OS: fuchsia, CPU: x64},
				Envs: []build.Environment{
					{Dimensions: build.DimensionSet{DeviceType: "AEMU"}},
					{Dimensions: build.DimensionSet{DeviceType: "other-device"}},
				},
			},
			{Test: build.Test{Name: "not-affected"}},
		}
		nameToShouldBeMultiplied := map[string]bool{
			"affected-arm64-Linux":                        false,
			"affected-linux-Linux":                        true,
			"affected-mac-Mac":                            false,
			"affected-host+target-AEMU":                   false,
			"affected-AEMU-AEMU":                          true,
			"affected-other-device-other-device":          false,
			"affected-AEMU-and-other-device-AEMU":         true,
			"affected-AEMU-and-other-device-other-device": false,
		}
		mods, err := AffectedModifiers(specs, affectedTests, maxAttempts, len(affectedTests))
		if err != nil {
			t.Errorf("AffectedModifiers() returned failed: %v", err)
		}
		for _, m := range mods {
			mod := m.Modifier
			shouldBeMultiplied := nameToShouldBeMultiplied[fmt.Sprintf("%s-%s", m.Test, environmentName(m.Env))]
			if shouldBeMultiplied {
				if mod.MaxAttempts != 0 {
					t.Errorf("%s.MaxAttempts is %d, want 0", mod.Name, mod.MaxAttempts)
				}
				if mod.TotalRuns != 0 {
					t.Errorf("%s.TotalRuns is %d, want 0", mod.Name, mod.MaxAttempts)
				}
			} else {
				if mod.MaxAttempts != maxAttempts {
					t.Errorf("%s.MaxAttempts is %d, want %d", mod.Name, mod.MaxAttempts, maxAttempts)
				}
				if !mod.Affected {
					t.Errorf("%s.Affected is false, want true", mod.Name)
				}
				if mod.TotalRuns >= 0 {
					t.Errorf("%s.TotalRuns is %d, want -1", mod.Name, mod.TotalRuns)
				}
			}
		}
	})
}

func TestMatchModifiersToTests(t *testing.T) {
	aemuEnv := build.Environment{Dimensions: build.DimensionSet{DeviceType: "AEMU"}}
	otherDeviceEnv := build.Environment{Dimensions: build.DimensionSet{DeviceType: "other-device"}}
	makeTestSpecs := func(count int, envs []build.Environment) []build.TestSpec {
		var specs []build.TestSpec
		for i := 0; i < count; i++ {
			specs = append(specs, build.TestSpec{
				Test: build.Test{Name: fullTestName(i, "fuchsia"), OS: fuchsia, CPU: x64},
				Envs: envs,
			})
		}
		return specs
	}

	cases := []struct {
		name        string
		specs       []build.TestSpec
		multipliers []TestModifier
		expected    []ModifierMatch
		err         error
	}{
		{
			name:  "one match per test-env",
			specs: makeTestSpecs(1, []build.Environment{aemuEnv, otherDeviceEnv}),
			multipliers: []TestModifier{
				{Name: "0", TotalRuns: 1},
			},
			expected: []ModifierMatch{
				{
					Test: fullTestName(0, "fuchsia"), Env: aemuEnv,
					Modifier: TestModifier{Name: "0", TotalRuns: 1},
				},
				{
					Test: fullTestName(0, "fuchsia"), Env: otherDeviceEnv,
					Modifier: TestModifier{Name: "0", TotalRuns: 1},
				},
			},
		},
		{
			name:  "uses regex matches if no tests match exactly",
			specs: makeTestSpecs(1, []build.Environment{aemuEnv}),
			multipliers: []TestModifier{
				{Name: "0", TotalRuns: 1},
			},
			expected: []ModifierMatch{{
				Test: fullTestName(0, "fuchsia"), Env: aemuEnv,
				Modifier: TestModifier{Name: "0", TotalRuns: 1},
			}},
		},
		{
			name: "prefer exact match over regex match",
			specs: []build.TestSpec{
				{
					Test: build.Test{Name: fullTestName(1, "fuchsia"), OS: fuchsia, CPU: x64},
					Envs: []build.Environment{aemuEnv},
				},
				{
					Test: build.Test{Name: fullTestName(10, "fuchsia"), OS: fuchsia, CPU: x64},
					Envs: []build.Environment{aemuEnv},
				},
			},
			multipliers: []TestModifier{
				{Name: fullTestName(1, "fuchsia"), TotalRuns: 1},
			},
			expected: []ModifierMatch{{
				Test: fullTestName(1, "fuchsia"), Env: aemuEnv,
				Modifier: TestModifier{Name: fullTestName(1, "fuchsia"), TotalRuns: 1},
			}},
		},
		{
			name:  "rejects multiplier that matches too many tests",
			specs: makeTestSpecs(maxMatchesPerMultiplier+1, []build.Environment{aemuEnv}),
			multipliers: []TestModifier{
				{Name: "fuchsia-pkg", TotalRuns: 1},
			},
			err: errTooManyMultiplierMatches,
		},
		{
			name:  "rejects invalid multiplier regex",
			specs: makeTestSpecs(1, []build.Environment{aemuEnv}),
			multipliers: []TestModifier{
				{Name: "["},
			},
			err: errInvalidMultiplierRegex,
		},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			actual, err := matchModifiersToTests(context.Background(), tc.specs, tc.multipliers)
			if !errors.Is(err, tc.err) {
				t.Fatalf("got err: %s, want %s", err, tc.err)
			}
			if diff := cmp.Diff(actual, tc.expected); diff != "" {
				t.Fatalf("unexpected ModifierMatches: (-got +want):\n%s", diff)
			}
		})
	}
}

// mkTempFile returns a new temporary file with the specified content that will
// be cleaned up automatically.
func mkTempFile(t *testing.T, content string) string {
	name := filepath.Join(t.TempDir(), "foo")
	if err := os.WriteFile(name, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	return name
}
