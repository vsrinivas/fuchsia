// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build"
)

var barTestModifier = TestModifier{
	Name:      "bar_tests",
	TotalRuns: 2,
}

var bazTestModifier = TestModifier{
	Name: "baz_host_tests",
	OS:   linux,
}

func TestLoadTestModifiers(t *testing.T) {
	areEqual := func(a, b []TestModifier) bool {
		stringify := func(modifier TestModifier) string {
			return fmt.Sprintf("%#v", modifier)
		}
		sort := func(list []TestModifier) {
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

	actual, err := LoadTestModifiers(modifiersPath)
	if err != nil {
		t.Fatalf("failed to load test modifiers: %v", err)
	}

	bazOut := bazTestModifier
	barOut := barTestModifier
	barOut.OS = ""
	expected := []TestModifier{barOut, bazOut}

	if !areEqual(expected, actual) {
		t.Fatalf("test modifiers not properly loaded:\nexpected:\n%+v\nactual:\n%+v", expected, actual)
	}
}

func TestAffectedModifiers(t *testing.T) {
	affectedTests := []string{
		"affected-arm64", "affected-linux", "affected-mac", "affected-host+target", "affected-AEMU", "affected-other-device",
	}
	name := mkTempFile(t, strings.Join(affectedTests, "\n"))
	const maxAttempts = 2
	t.Run("not multiplied if over threshold", func(t *testing.T) {
		mods, err := AffectedModifiers(nil, name, maxAttempts, len(affectedTests)-1)
		if err != nil {
			t.Errorf("AffectedModifiers() returned failed: %v", err)
		}
		for _, mod := range mods {
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
			{Test: build.Test{Name: "affected-host+target", OS: linux, CPU: x64},
				Envs: []build.Environment{{Dimensions: build.DimensionSet{DeviceType: "AEMU"}}}},
			{Test: build.Test{Name: "affected-AEMU", OS: fuchsia, CPU: x64},
				Envs: []build.Environment{{Dimensions: build.DimensionSet{DeviceType: "AEMU"}}}},
			{Test: build.Test{Name: "affected-other-device", OS: fuchsia, CPU: x64},
				Envs: []build.Environment{{Dimensions: build.DimensionSet{DeviceType: "other-device"}}}},
			{Test: build.Test{Name: "not-affected"}},
		}
		nameToShouldBeMultiplied := map[string]bool{
			"affected-arm64":        false,
			"affected-linux":        true,
			"affected-mac":          false,
			"affected-host+target":  false,
			"affected-AEMU":         true,
			"affected-other-device": false,
		}
		mods, err := AffectedModifiers(specs, name, maxAttempts, len(affectedTests))
		if err != nil {
			t.Errorf("AffectedModifiers() returned failed: %v", err)
		}
		for _, mod := range mods {
			shouldBeMultiplied := nameToShouldBeMultiplied[mod.Name]
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

// mkTempFile returns a new temporary file with the specified content that will
// be cleaned up automatically.
func mkTempFile(t *testing.T, content string) string {
	name := filepath.Join(t.TempDir(), "foo")
	if err := ioutil.WriteFile(name, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	return name
}
