// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"fmt"
	"sort"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/build"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"google.golang.org/protobuf/testing/protocmp"
	"google.golang.org/protobuf/types/known/structpb"
)

type fakeBuildModules struct {
	archives         []build.Archive
	generatedSources []string
	images           []build.Image
	pbinSets         []build.PrebuiltBinarySet
	testSpecs        []build.TestSpec
	tools            build.Tools
	zbiTests         []build.ZBITest
}

func (m fakeBuildModules) Archives() []build.Archive                     { return m.archives }
func (m fakeBuildModules) GeneratedSources() []string                    { return m.generatedSources }
func (m fakeBuildModules) Images() []build.Image                         { return m.images }
func (m fakeBuildModules) PrebuiltBinarySets() []build.PrebuiltBinarySet { return m.pbinSets }
func (m fakeBuildModules) TestSpecs() []build.TestSpec                   { return m.testSpecs }
func (m fakeBuildModules) Tools() build.Tools                            { return m.tools }
func (m fakeBuildModules) ZBITests() []build.ZBITest                     { return m.zbiTests }

func TestConstructNinjaTargets(t *testing.T) {
	testCases := []struct {
		name              string
		staticSpec        *fintpb.Static
		modules           fakeBuildModules
		expectedTargets   []string
		expectedArtifacts *fintpb.BuildArtifacts
		expectErr         bool
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
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltImages: []*structpb.Struct{
					mustStructPB(t, build.Image{Name: qemuImageNames[0], Path: "qemu_image_path"}),
				},
			},
		},
		{
			name: "images and archives included",
			staticSpec: &fintpb.Static{
				IncludeImages:   true,
				IncludeArchives: true,
			},
			modules: fakeBuildModules{
				archives: []build.Archive{
					{Name: "packages", Path: "p.tar.gz", Type: "tgz"},
					{Name: "archive", Path: "b.tar", Type: "tar"},
					{Name: "archive", Path: "b.tgz", Type: "tgz"},
					{Name: "other", Path: "other.tgz", Type: "tgz"},
				},
			},
			expectedTargets: append(extraTargetsForImages, "p.tar.gz", "b.tgz"),
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltArchives: []*structpb.Struct{
					mustStructPB(t, build.Archive{Name: "packages", Path: "p.tar.gz", Type: "tgz"}),
					mustStructPB(t, build.Archive{Name: "archive", Path: "b.tgz", Type: "tgz"}),
				},
			},
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
		{
			name: "prebuilt binary manifests included",
			staticSpec: &fintpb.Static{
				IncludePrebuiltBinaryManifests: true,
			},
			modules: fakeBuildModules{
				pbinSets: []build.PrebuiltBinarySet{
					{Manifest: "manifest1.json"},
					{Manifest: "manifest2.json"},
				},
			},
			expectedTargets: []string{"manifest1.json", "manifest2.json"},
		},
		{
			name: "tools included",
			staticSpec: &fintpb.Static{
				Tools: []string{"tool1", "tool2"},
			},
			modules: fakeBuildModules{
				tools: makeTools(map[string][]string{
					"tool1": {"linux", "mac"},
					"tool2": {"linux"},
					"tool3": {"linux", "mac"},
				}),
			},
			expectedTargets: []string{"linux_x64/tool1", "linux_x64/tool2"},
		},
		{
			name: "nonexistent tool",
			staticSpec: &fintpb.Static{
				Tools: []string{"tool1"},
			},
			expectErr: true,
		},
		{
			name: "tool not supported on current platform",
			staticSpec: &fintpb.Static{
				Tools: []string{"tool1"},
			},
			modules: fakeBuildModules{
				tools: makeTools(map[string][]string{
					"tool1": {"mac"},
				}),
			},
			expectErr: true,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			// Normalize expected data so tests are less painful to write.
			sort.Strings(tc.expectedTargets)
			if tc.expectedArtifacts == nil {
				tc.expectedArtifacts = &fintpb.BuildArtifacts{}
			}

			targets, artifacts, err := constructNinjaTargets(tc.modules, tc.staticSpec, "linux-x64")
			if err != nil {
				if tc.expectErr {
					return
				}
				t.Fatal(err)
			} else if tc.expectErr {
				t.Fatalf("Expected an error, but got nil")
			}
			if diff := cmp.Diff(tc.expectedTargets, targets); diff != "" {
				t.Errorf("Got wrong targets (-want +got):\n%s", diff)
			}
			if diff := cmp.Diff(tc.expectedArtifacts, artifacts, protocmp.Transform()); diff != "" {
				t.Errorf("Got wrong artifacts (-want +got):\n%s", diff)
			}
		})
	}
}

func makeTools(supportedOSes map[string][]string) build.Tools {
	var res build.Tools
	for toolName, systems := range supportedOSes {
		for _, os := range systems {
			res = append(res, build.Tool{
				Name: toolName,
				OS:   os,
				CPU:  "x64",
				Path: fmt.Sprintf("%s_x64/%s", os, toolName),
			})
		}
	}
	return res
}

// mustStructPB converts a Go struct to a protobuf Struct, failing the test in
// case of failure.
func mustStructPB(t *testing.T, s interface{}) *structpb.Struct {
	ret, err := toStructPB(s)
	if err != nil {
		t.Fatal(err)
	}
	return ret
}
