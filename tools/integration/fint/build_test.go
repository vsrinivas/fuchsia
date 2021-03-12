// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"sort"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/build"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
)

type fakeBuildModules struct {
	archives         []build.Archive
	generatedSources []string
	images           []build.Image
	testSpecs        []build.TestSpec
	zbiTests         []build.ZBITest
}

func (m fakeBuildModules) Archives() []build.Archive   { return m.archives }
func (m fakeBuildModules) GeneratedSources() []string  { return m.generatedSources }
func (m fakeBuildModules) Images() []build.Image       { return m.images }
func (m fakeBuildModules) TestSpecs() []build.TestSpec { return m.testSpecs }
func (m fakeBuildModules) ZBITests() []build.ZBITest   { return m.zbiTests }

func TestConstructNinjaTargets(t *testing.T) {
	testCases := []struct {
		name            string
		staticSpec      *fintpb.Static
		modules         fakeBuildModules
		expectedTargets []string
	}{
		{
			name:            "empty spec produces no ninja targets",
			staticSpec:      &fintpb.Static{},
			expectedTargets: nil,
		},
		{
			name: "extra ad-hoc ninja targets",
			staticSpec: &fintpb.Static{
				NinjaTargets: []string{"foo", "bar"},
			},
			expectedTargets: []string{"foo", "bar"},
		},
		{
			name: "images for testing included",
			staticSpec: &fintpb.Static{
				IncludeImages: true,
			},
			modules: fakeBuildModules{
				images: []build.Image{
					{Name: qemuImageNames[0], Path: "qemu_image_path"},
					{Name: "should-be-ignored", Path: "different_path"},
				},
			},
			expectedTargets: append(extraTargetsForImages, "qemu_image_path", "build/images:updates"),
		},
		{
			name: "images and archives included",
			staticSpec: &fintpb.Static{
				IncludeImages:   true,
				IncludeArchives: true,
			},
			modules: fakeBuildModules{
				archives: []build.Archive{
					{Name: "packages", Path: "p.tar.gz"},
					{Name: "archive", Path: "b.tar"},
					{Name: "archive", Path: "b.tgz"},
					{Name: "other", Path: "other.tgz"},
				},
			},
			expectedTargets: append(extraTargetsForImages, "p.tar.gz", "b.tar", "b.tgz"),
		},
		{
			name: "host tests included",
			staticSpec: &fintpb.Static{
				IncludeHostTests: true,
			},
			modules: fakeBuildModules{
				testSpecs: []build.TestSpec{
					{Test: build.Test{OS: "fuchsia", Path: "fuchsia_path"}},
					{Test: build.Test{OS: "linux", Path: "linux_path"}},
					{Test: build.Test{OS: "mac", Path: "mac_path"}},
				},
			},
			expectedTargets: []string{"linux_path", "mac_path"},
		},
		{
			name: "zbi tests included",
			staticSpec: &fintpb.Static{
				IncludeZbiTests: true,
			},
			modules: fakeBuildModules{
				zbiTests: []build.ZBITest{
					{Path: "foo.zbi"},
					{Path: "bar.zbi"},
				},
			},
			expectedTargets: []string{"foo.zbi", "bar.zbi"},
		},
		{
			name: "generated sources included",
			staticSpec: &fintpb.Static{
				IncludeGeneratedSources: true,
			},
			modules: fakeBuildModules{
				generatedSources: []string{"foo.h", "bar.h"},
			},
			expectedTargets: []string{"foo.h", "bar.h"},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			sort.Strings(tc.expectedTargets)
			got := constructNinjaTargets(tc.modules, tc.staticSpec)
			if diff := cmp.Diff(tc.expectedTargets, got); diff != "" {
				t.Fatalf("Got wrong targets (-want +got):\n%s", diff)
			}
		})
	}
}
