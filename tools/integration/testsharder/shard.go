// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/repo"
	"go.fuchsia.dev/fuchsia/tools/build"
)

// Shard represents a set of tests with a common execution environment.
type Shard struct {
	// Name is the identifier for the shard.
	Name string `json:"name"`

	// Tests is the set of tests to be executed in this shard.
	Tests []Test `json:"tests"`

	// Env is a generalized notion of the execution environment for the shard.
	Env build.Environment `json:"environment"`

	// Deps is the list of runtime dependencies required to be present on the host
	// at shard execution time. It is a list of paths relative to the fuchsia
	// build directory.
	Deps []string `json:"deps,omitempty"`

	// PkgRepo is the path to the shard-specific package repository. It is
	// relative to the fuchsia build directory, and is a directory itself.
	PkgRepo string `json:"pkg_repo,omitempty"`

	// TimeoutSecs is the execution timeout, in seconds, that should be set for
	// the task that runs the shard. It's computed dynamically based on the
	// expected runtime of the tests.
	TimeoutSecs int `json:"timeout_secs"`
}

// CreatePackageRepo creates a package repository for the given shard.
func (s *Shard) CreatePackageRepo() error {
	// Aggregate the package manifests used by the shard. We do this first
	// as we can early exit for shards with no such manifests.
	var pkgManifests []string
	for _, t := range s.Tests {
		if len(t.PackageManifests) != 0 {
			pkgManifests = append(pkgManifests, t.PackageManifests...)
		}
	}
	if len(pkgManifests) == 0 {
		return nil
	}

	// run-test-suite and run-test-component are used by all shards, so
	// always add their package manifests to the list.
	// TODO(rudymathu): This is very much a hack to get the CAS builders
	// up and running to evaluate stability and performance. In the long
	// term, this should be handled by the build system and passed in.
	pkgManifests = append(pkgManifests,
		"obj/garnet/bin/run_test_component/run-test-component-pkg/package_manifest.json",
		"obj/src/sys/run_test_suite/run_test_suite_pkg/package_manifest.json",
	)

	// The path to the package repository should be unique so as to not
	// conflict with other shards' repositories. Ideally we'd just use
	// the shard name, but that can include nonstandard characters, so
	// we use the base64 encoding just in case.
	repoPath := fmt.Sprintf("repo_%s", base64.StdEncoding.EncodeToString([]byte(s.Name)))

	// Clean out any existing repositories for this shard. Theoretically,
	// we could build on top of previous repositories, but this seems a
	// bit unsafe and prone to monotonically increasing repo sizes.
	if err := os.RemoveAll(repoPath); err != nil {
		return err
	}

	// Initialize the empty repository.
	r, err := repo.New(repoPath, filepath.Join(repoPath, "repository", "blobs"))
	if err != nil {
		return err
	}
	if err := r.Init(); err != nil {
		return err
	}

	// While this looks like a no-op, the underlying pm library uses it to
	// generate targets/snapshot/timestamp metadata.
	if err := r.AddTargets([]string{}, json.RawMessage{}); err != nil {
		return err
	}

	// Publish the package manifests to the new repository.
	if _, err := r.PublishManifests(pkgManifests); err != nil {
		return err
	}
	if err := r.CommitUpdates(true); err != nil {
		return err
	}
	s.PkgRepo = repoPath
	s.AddDeps([]string{repoPath})
	return nil
}

// AddDeps adds a set of runtime dependencies to the shard. It ensures no
// duplicates and a stable ordering.
func (s *Shard) AddDeps(deps []string) {
	s.Deps = append(s.Deps, deps...)
	s.Deps = dedupe(s.Deps)
	sort.Strings(s.Deps)
}

func dedupe(l []string) []string {
	var deduped []string
	m := make(map[string]struct{})
	for _, s := range l {
		m[s] = struct{}{}
	}
	for s := range m {
		deduped = append(deduped, s)
	}
	return deduped
}

// ShardOptions parametrize sharding behavior.
type ShardOptions struct {
	// Mode is a general mode in which the testsharder will be run. See mode.go
	// for more details.
	Mode Mode

	// Tags is the list of tags that the sharded Environments must match; those
	// that don't match all tags will be ignored.
	Tags []string
}

// MakeShards returns the list of shards associated with a given build.
// A single output shard will contain only tests that have the same environment.
func MakeShards(specs []build.TestSpec, opts *ShardOptions) []*Shard {
	// Collect the order of the shards so our shard ordering is deterministic with
	// respect to the input.
	envToSuites := newEnvMap()
	envs := []build.Environment{}
	for _, spec := range specs {
		for _, env := range spec.Envs {
			if !stringSlicesEq(opts.Tags, env.Tags) {
				continue
			}
			if opts.Mode == Restricted && env.ServiceAccount != "" {
				continue
			}

			// Tags should not differ by ordering.
			sortableTags := sort.StringSlice(opts.Tags)
			sortableTags.Sort()
			env.Tags = []string(sortableTags)

			specs, ok := envToSuites.get(env)
			if !ok {
				envs = append(envs, env)
			}
			envToSuites.set(env, append(specs, spec))
		}
	}
	shards := make([]*Shard, 0, len(envs))
	for _, env := range envs {
		specs, _ := envToSuites.get(env)
		sort.Slice(specs, func(i, j int) bool {
			return specs[i].Test.Name < specs[j].Test.Name
		})
		tests := []Test{}
		for _, spec := range specs {
			tests = append(tests, Test{Test: spec.Test, Runs: 1})
		}
		shards = append(shards, &Shard{
			Name:  environmentName(env),
			Tests: tests,
			Env:   env,
		})
	}
	return shards
}

// EnvironmentName returns a name for an environment.
func environmentName(env build.Environment) string {
	tokens := []string{}
	addToken := func(s string) {
		if s != "" {
			// s/-/_, so there is no ambiguity among the tokens
			// making up a name.
			s = strings.Replace(s, "-", "_", -1)
			tokens = append(tokens, s)
		}
	}

	addToken(env.Dimensions.DeviceType)
	addToken(env.Dimensions.OS)
	addToken(env.Dimensions.Testbed)
	addToken(env.Dimensions.Pool)
	if env.ServiceAccount != "" {
		addToken(strings.Split(env.ServiceAccount, "@")[0])
	}
	if env.Netboot {
		addToken("netboot")
	}
	return strings.Join(tokens, "-")
}

// Abstracts a mapping build.Environment -> []string, as build.Environment contains non-comparable
// members (e.g., string slices), which makes it invalid for a map key.
type envMap struct {
	m map[string][]build.TestSpec
}

func newEnvMap() envMap {
	return envMap{m: make(map[string][]build.TestSpec)}
}

func (em envMap) get(e build.Environment) ([]build.TestSpec, bool) {
	specs, ok := em.m[fmt.Sprintf("%v", e)]
	return specs, ok
}

func (em *envMap) set(e build.Environment, specs []build.TestSpec) {
	em.m[fmt.Sprintf("%v", e)] = specs
}

func stringSlicesEq(s []string, t []string) bool {
	if len(s) != len(t) {
		return false
	}
	seen := make(map[string]int)
	for i := range s {
		seen[s[i]]++
		seen[t[i]]--
	}
	for _, v := range seen {
		if v != 0 {
			return false
		}
	}
	return true
}
