// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package testsharder

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"

	"fuchsia.googlesource.com/tools/build"
)

const (
	// TestSpecSuffix is the file suffix identifying a test spec.
	TestSpecSuffix = ".spec.json"

	// TestDepsSuffix is the file suffix identifying a file giving the runtime
	// depedencies of a test.
	TestDepsSuffix = ".spec.data"
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
	OS string `json:"os"`

	// Command is the command line to run to execute this test.
	Command []string `json:"command,omitempty"`

	// Deps is the list of paths to the test's runtime dependencies,
	// relative to the build directory.
	// Currently this field only makes sense for Linux and Mac tests.
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

// PkgTestSpecDir returns the directory where associated test specs may be written,
// given a package target.
func PkgTestSpecDir(fuchsiaBuildDir string, pkg build.Target) string {
	return filepath.Join(fuchsiaBuildDir, pkg.BuildDir, pkg.Name)
}

// HostTestSpecDir returns the directory where associated test specs may be written,
// given a host test target.
func HostTestSpecDir(fuchsiaBuildDir string, hostTest build.Target) string {
	return filepath.Join(fuchsiaBuildDir, hostTest.BuildDir)
}

func readLinesFromFile(path string) ([]string, error) {
	fd, err := os.Open(path)
	defer fd.Close()
	if err != nil {
		return nil, fmt.Errorf("failed to open %s: %v", path, err)
	}

	reader := bufio.NewReader(fd)
	lines := []string{}
	for {
		line, err := reader.ReadString('\n')
		if err == io.EOF {
			break
		} else if err != nil {
			return nil, fmt.Errorf("failed to read line from %s: %v", path, err)
		}
		line = strings.TrimRight(line, "\n")
		lines = append(lines, line)
	}
	return lines, nil
}

// LoadTestSpecs loads a set of test specifications from a list of Fuchsia
// package targets and a list of host test targets.
func LoadTestSpecs(fuchsiaBuildDir string, pkgs, hostTests []build.Target) ([]TestSpec, error) {
	// First, load the test specs associated to the given packages.
	//
	// It is guaranteed that a test spec will be written to the build directory of the
	// corresponding package its test was defined in: specifically, it will be put in
	// <target_out_dir of the test package>/<test package name>.
	specs := []TestSpec{}

	decodeTestSpecs := func(targets []build.Target, testSpecDir func(string, build.Target) string) error {
		for _, target := range targets {
			specDir := testSpecDir(fuchsiaBuildDir, target)
			// If the associated test spec directory does not exist, the package specified no
			// tests.
			if _, err := os.Stat(specDir); os.IsNotExist(err) {
				continue
			}

			// Non-recursively enumerate the files in this directory; it's guaranteed that
			// the test specs will be found here if generated.
			entries, err := ioutil.ReadDir(specDir)
			if err != nil {
				return err
			}

			for _, entry := range entries {
				if entry.IsDir() {
					continue
				}

				// Open, read, and parse any test spec found. Look for any associated
				// runtime depedencies.
				path := filepath.Join(specDir, entry.Name())
				if strings.HasSuffix(path, TestSpecSuffix) {
					specFile, err := os.Open(path)
					defer specFile.Close()
					if err != nil {
						return fmt.Errorf("failed to open %s: %v", path, err)
					}

					var spec TestSpec
					if err := json.NewDecoder(specFile).Decode(&spec); err != nil {
						return fmt.Errorf("failed to decode %s: %v", path, err)
					}

					testDepsPath := strings.Replace(path, TestSpecSuffix, TestDepsSuffix, 1)
					_, err = os.Stat(testDepsPath)
					if err == nil {
						deps, err := readLinesFromFile(testDepsPath)
						if err != nil {
							return err
						}
						spec.Test.Deps = deps
					} else if !os.IsNotExist(err) {
						return err
					}
					specs = append(specs, spec)
				}
			}
		}
		return nil
	}

	if err := decodeTestSpecs(pkgs, PkgTestSpecDir); err != nil {
		return nil, err
	}
	if err := decodeTestSpecs(hostTests, HostTestSpecDir); err != nil {
		return nil, err
	}

	return specs, nil
}
