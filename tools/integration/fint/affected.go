// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"

	"go.fuchsia.dev/fuchsia/tools/build"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/hostplatform"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
)

// Affected does a ninja dry run and analyzes the output, to determine:
// a) If the build graph is affected by the changed files.
// b) If so, which tests are affected by the changed files.
func Affected(ctx context.Context, staticSpec *fintpb.Static, contextSpec *fintpb.Context) (*fintpb.BuildArtifacts, error) {
	platform, err := hostplatform.Name()
	if err != nil {
		return nil, err
	}
	modules, err := build.NewModules(contextSpec.BuildDir)
	if err != nil {
		return nil, err
	}
	if contextSpec.ArtifactDir != "" && len(contextSpec.ChangedFiles) > 0 && len(modules.TestSpecs()) > 0 {
		return &fintpb.BuildArtifacts{}, nil
	}
	ninjaTargets, _, err := constructNinjaTargets(modules, staticSpec, contextSpec, platform)
	if err != nil {
		return &fintpb.BuildArtifacts{}, err
	}
	artifacts, err := affectedImpl(ctx, &subprocess.Runner{}, staticSpec, contextSpec, modules, platform, ninjaTargets)
	if err != nil && artifacts != nil && artifacts.FailureSummary == "" {
		// Fall back to using the error text as the failure summary if the
		// failure summary is unset. It's better than failing without emitting
		// any information.
		artifacts.FailureSummary = err.Error()
	}
	return artifacts, err

}

// affectedImpl contains the business logic of `fint affected`, extracted into
// a function that is more easily callable from buildImpl.
func affectedImpl(
	ctx context.Context,
	runner subprocessRunner,
	staticSpec *fintpb.Static,
	contextSpec *fintpb.Context,
	modules buildModules,
	platform string,
	ninjaTargets []string,
) (*fintpb.BuildArtifacts, error) {
	artifacts := &fintpb.BuildArtifacts{}

	ninjaPath, err := toolAbsPath(modules, contextSpec.BuildDir, platform, "ninja")
	if err != nil {
		return artifacts, err
	}
	r := ninjaRunner{
		runner:    runner,
		ninjaPath: ninjaPath,
		buildDir:  contextSpec.BuildDir,
		jobCount:  int(contextSpec.GomaJobCount),
	}

	if contextSpec.ArtifactDir != "" && len(contextSpec.ChangedFiles) > 0 && len(modules.TestSpecs()) > 0 {
		var tests []build.Test
		for _, t := range modules.TestSpecs() {
			tests = append(tests, t.Test)
		}
		result, err := affectedTestsNoWork(ctx, r, contextSpec, tests, ninjaTargets)
		if err != nil {
			return artifacts, err
		}
		if err := saveLogs(contextSpec.ArtifactDir, artifacts, result.logs); err != nil {
			return artifacts, err
		}
		artifacts.AffectedTests = result.affectedTests
		artifacts.BuildNotAffected = result.noWork
	}
	return artifacts, nil
}
