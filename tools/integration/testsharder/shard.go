// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"fmt"
	"io"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strings"

	pm_build "go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/build"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

const (
	// The name of the metadata directory within a package repository.
	metadataDirName = "repository"
	// The name of the blobs directory within a package repository.
	blobsDirName = "blobs"
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

	// Summary is a TestSummary that is populated if the shard is skipped.
	Summary runtests.TestSummary `json:"summary,omitempty"`
}

// CreatePackageRepo creates a package repository for the given shard.
func (s *Shard) CreatePackageRepo(buildDir string, globalRepoMetadata string, cacheTestPackages bool) error {
	globalRepoMetadata = filepath.Join(buildDir, globalRepoMetadata)

	// The path to the package repository should be unique so as to not
	// conflict with other shards' repositories.
	localRepoRel := fmt.Sprintf("repo_%s", url.PathEscape(s.Name))
	localRepo := filepath.Join(buildDir, localRepoRel)
	// Remove the localRepo if it exists in the incremental build cache.
	if err := os.RemoveAll(localRepo); err != nil {
		return err
	}

	// Copy over all repository metadata (encoded in JSON files).
	localRepoMetadata := filepath.Join(localRepo, metadataDirName)
	if err := os.MkdirAll(localRepoMetadata, os.ModePerm); err != nil {
		return err
	}
	entries, err := os.ReadDir(globalRepoMetadata)
	if err != nil {
		return err
	}
	for _, e := range entries {
		filename := e.Name()
		if filepath.Ext(filename) == ".json" {
			src := filepath.Join(globalRepoMetadata, filename)
			dst := filepath.Join(localRepoMetadata, filename)
			if err := os.Link(src, dst); err != nil {
				return err
			}
		}
	}
	// Add the blobs we expect the shard to access if the caller wants us to
	// set up a local package cache.
	if cacheTestPackages {
		var pkgManifests []string
		for _, t := range s.Tests {
			pkgManifests = append(pkgManifests, t.PackageManifests...)
		}

		blobsDir := filepath.Join(localRepo, blobsDirName)
		addedBlobs := make(map[string]struct{})
		if err := os.Mkdir(blobsDir, os.ModePerm); err != nil {
			return err
		}
		for _, p := range pkgManifests {
			manifest, err := pm_build.LoadPackageManifest(filepath.Join(buildDir, p))
			if err != nil {
				return err
			}
			for _, blob := range manifest.Blobs {
				if _, exists := addedBlobs[blob.Merkle.String()]; !exists {
					src := filepath.Join(buildDir, blob.SourcePath)
					dst := filepath.Join(blobsDir, blob.Merkle.String())
					if err := linkOrCopy(src, dst); err != nil {
						return err
					}
					addedBlobs[blob.Merkle.String()] = struct{}{}
				}
			}
		}
	}

	s.PkgRepo = localRepoRel
	s.AddDeps([]string{localRepoRel})
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
	// Tags is the list of tags that the sharded Environments must match; those
	// that don't match all tags will be ignored.
	Tags []string
}

// MakeShards returns the list of shards associated with a given build.
// A single output shard will contain only tests that have the same environment.
func MakeShards(specs []build.TestSpec, testListEntries map[string]build.TestListEntry, opts *ShardOptions) []*Shard {
	// We don't want to crash if we've passed a nil testListEntries map.
	if testListEntries == nil {
		testListEntries = make(map[string]build.TestListEntry)
	}
	// Collect the order of the shards so our shard ordering is deterministic with
	// respect to the input.
	envToSuites := newEnvMap()
	envs := []build.Environment{}
	for _, spec := range specs {
		for _, env := range spec.Envs {
			if !stringSlicesEq(opts.Tags, env.Tags) {
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
			test := Test{Test: spec.Test, Runs: 1}
			testListEntry, exists := testListEntries[spec.Test.Name]
			if exists {
				test.applyTestListTags(testListEntry)
			}
			if spec.Test.Isolated {
				shards = append(shards, &Shard{
					Name:  fmt.Sprintf("%s-%s", environmentName(env), normalizeTestName(spec.Test.Name)),
					Tests: []Test{test},
					Env:   env,
				})
			} else {
				tests = append(tests, test)
			}
		}
		if len(tests) > 0 {
			shards = append(shards, &Shard{
				Name:  environmentName(env),
				Tests: tests,
				Env:   env,
			})
		}
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
	for _, name := range env.ExtraEnvNameKeys {
		addToken(name)
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

// linkOrCopy hardlinks src to dst if src is not a symlink. If the source is a
// symlink, then it copies it. There are several blobs in the build directory
// that are symlinks to CIPD packages, and we don't want to include that
// symlink in the final package repository, so we copy instead.
func linkOrCopy(src string, dst string) error {
	info, err := os.Lstat(src)
	if err != nil {
		return err
	}
	if info.Mode()&os.ModeSymlink != os.ModeSymlink {
		return os.Link(src, dst)
	}
	s, err := os.Open(src)
	if err != nil {
		return err
	}
	defer s.Close()
	d, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer d.Close()
	_, err = io.Copy(d, s)
	return err
}
