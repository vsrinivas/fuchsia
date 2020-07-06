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
	"testing"
)

var barTestModifier = TestModifier{
	Name:      "bar_tests",
	TotalRuns: 2,
}

var bazTestModifier = TestModifier{
	Name: "baz_host_tests",
	OS:   "linux",
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

	tmpDir, err := ioutil.TempDir("", "test-spec")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

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
