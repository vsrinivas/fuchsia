// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package testsharder

import (
	"encoding/json"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

func deepCopy(x, y interface{}) error {
	b, err := json.Marshal(x)
	if err != nil {
		return err
	}
	return json.Unmarshal(b, y)
}

func TestValidation(t *testing.T) {
	platforms := []build.DimensionSet{
		{
			DeviceType: "qemu",
			CPU:        "arm64",
		},
		{
			DeviceType: "nuc",
			CPU:        "x64",
		},
		{
			OS:  "Linux",
			CPU: "x64",
		},
	}

	validate := func(t *testing.T, specs []build.TestSpec, expectSuccess bool) {
		if err := ValidateTests(specs, platforms); (err == nil) != expectSuccess {
			t.Fatal(err)
		}
	}

	genericTestSpec := build.TestSpec{
		Test: build.Test{
			Name: "//src/foo:tests",
			Path: "path/to/test",
			OS:   "linux",
		},
		Envs: []build.Environment{
			{
				Dimensions: build.DimensionSet{
					OS: "Linux",
				},
			},
		},
	}

	// Returns a deep copy of genericTestSpec.
	getSpec := func(t *testing.T) build.TestSpec {
		var spec build.TestSpec
		if err := deepCopy(&genericTestSpec, &spec); err != nil {
			t.Fatalf("failed to make a deep copy: %v", err)
		}
		return spec
	}

	t.Run("test with no name is invalid", func(t *testing.T) {
		spec := getSpec(t)
		spec.Name = ""
		validate(t, []build.TestSpec{spec}, false)
	})
	t.Run("test with no install path nor command is invalid", func(t *testing.T) {
		spec := getSpec(t)
		spec.Path = ""
		validate(t, []build.TestSpec{spec}, false)
	})
	t.Run("test with no OS is invalid", func(t *testing.T) {
		spec := getSpec(t)
		spec.OS = ""
		validate(t, []build.TestSpec{spec}, false)
	})
	t.Run("test with a non-matching environment is invalid", func(t *testing.T) {
		spec := getSpec(t)
		spec.Envs = []build.Environment{
			{
				Dimensions: build.DimensionSet{
					OS: "Mac",
				},
			},
		}
		validate(t, []build.TestSpec{spec}, false)
	})
	t.Run("test with no environments is valid", func(t *testing.T) {
		spec := getSpec(t)
		spec.Envs = nil
		validate(t, []build.TestSpec{spec}, true)
	})
	t.Run("test with a matching environment is valid", func(t *testing.T) {
		spec := getSpec(t)
		validate(t, []build.TestSpec{spec}, true)
	})
}
