// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"
	"google.golang.org/protobuf/types/known/structpb"

	"go.fuchsia.dev/fuchsia/tools/build"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
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

func TestBuild(t *testing.T) {
	platform := "linux-x64"
	artifactDir := filepath.Join(t.TempDir(), "artifacts")
	resetArtifactDir := func(t *testing.T) {
		// `artifactDir` is in the top-level tempdir so it can be referenced
		// in the `testCases` table, but that means it doesn't get cleared
		// between sub-tests so we need to clear it explicitly.
		if err := os.RemoveAll(artifactDir); err != nil {
			t.Fatal(err)
		}
		if err := os.MkdirAll(artifactDir, 0o700); err != nil {
			t.Fatal(err)
		}
	}
	testCases := []struct {
		name              string
		staticSpec        *fintpb.Static
		contextSpec       *fintpb.Context
		modules           fakeBuildModules
		expectedArtifacts *fintpb.BuildArtifacts
		expectErr         bool
	}{
		{
			name:              "empty spec produces no ninja targets",
			staticSpec:        &fintpb.Static{},
			expectedArtifacts: &fintpb.BuildArtifacts{},
		},
		{
			name: "extra ad-hoc ninja targets",
			staticSpec: &fintpb.Static{
				NinjaTargets: []string{"foo", "bar"},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"foo", "bar"},
			},
		},
		{
			name: "duplicate targets",
			staticSpec: &fintpb.Static{
				NinjaTargets: []string{"foo", "foo"},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"foo"},
			},
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
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltImages: []*structpb.Struct{
					mustStructPB(t, build.Image{Name: qemuImageNames[0], Path: "qemu_image_path"}),
				},
				BuiltTargets: append(extraTargetsForImages, "build/images:updates", "qemu_image_path"),
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
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltArchives: []*structpb.Struct{
					mustStructPB(t, build.Archive{Name: "packages", Path: "p.tar.gz", Type: "tgz"}),
					mustStructPB(t, build.Archive{Name: "archive", Path: "b.tgz", Type: "tgz"}),
				},
				BuiltTargets: append(extraTargetsForImages, "p.tar.gz", "b.tgz"),
			},
		},
		{
			name: "netboot images and scripts excluded when paving",
			staticSpec: &fintpb.Static{
				Pave:            true,
				IncludeImages:   true,
				IncludeArchives: true,
			},
			modules: fakeBuildModules{
				images: []build.Image{
					{Name: "netboot", Path: "netboot.zbi", Type: "zbi"},
					{Name: "netboot-script", Path: "netboot.sh", Type: "script"},
					{Name: "foo", Path: "foo.sh", Type: "script"},
				},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltImages: []*structpb.Struct{
					mustStructPB(t, build.Image{Name: "foo", Path: "foo.sh", Type: "script"}),
				},
				BuiltTargets: append(extraTargetsForImages, "foo.sh"),
			},
		},
		{
			name: "default ninja target included",
			staticSpec: &fintpb.Static{
				IncludeDefaultNinjaTarget: true,
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{":default"},
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
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"linux_path", "mac_path"},
			},
		},
		{
			name: "generated sources included",
			staticSpec: &fintpb.Static{
				IncludeGeneratedSources: true,
			},
			modules: fakeBuildModules{
				generatedSources: []string{"foo.h", "bar.h"},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"foo.h", "bar.h"},
			},
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
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"manifest1.json", "manifest2.json"},
			},
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
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"linux_x64/tool1", "linux_x64/tool2"},
			},
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
		{
			name: "zbi tests",
			staticSpec: &fintpb.Static{
				TargetArch:      fintpb.Static_ARM64,
				IncludeZbiTests: true,
			},
			modules: fakeBuildModules{
				zbiTests: []build.ZBITest{
					{
						Name:        "foo",
						Label:       "//src/foo",
						DeviceTypes: []string{"AEMU"},
						Path:        "foo.zbi",
					},
					{
						Name:        "bar",
						Label:       "//src/bar",
						DeviceTypes: []string{"Intel NUC Kit NUC7i5DNHE"},
						Path:        "bar.zbi",
					},
				},
				images: []build.Image{
					{
						Name:  qemuKernelImageName,
						Label: "//src/foo",
						Path:  "foo-qemu-kernel",
					},
					{
						Name: "fastboot",
						Path: "fastboot",
					},
					{
						Name:            "zircon-a",
						PaveZedbootArgs: []string{"--boot", "--zircona"},
						Path:            "zircona",
					},
					{
						Name:            "zircon-r",
						PaveZedbootArgs: []string{"--zirconr"},
						Path:            "zirconr",
					},
				},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltZedbootImages: []*structpb.Struct{
					mustStructPB(t, build.Image{
						Name:            "zircon-a",
						PaveZedbootArgs: []string{"--boot", "--zircona", "--zirconb"},
						Path:            "zircona",
					}),
					mustStructPB(t, build.Image{
						Name:            "zircon-r",
						PaveZedbootArgs: []string{"--zirconr"},
						Path:            "zirconr",
					}),
				},
				BuiltTargets: []string{"foo.zbi", "bar.zbi", "foo-qemu-kernel", "zircona", "zirconr"},
				ZbiTestQemuKernelImages: map[string]*structpb.Struct{
					"foo": mustStructPB(t, build.Image{
						Name:  qemuKernelImageName,
						Label: "//src/foo",
						Path:  "foo-qemu-kernel",
						Type:  "kernel",
					}),
				},
			},
		},
	}
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			resetArtifactDir(t)

			checkoutDir := t.TempDir()
			buildDir := filepath.Join(t.TempDir(), "out", "default")

			defaultContextSpec := &fintpb.Context{
				SkipNinjaNoopCheck: true,
				CheckoutDir:        checkoutDir,
				BuildDir:           buildDir,
			}
			proto.Merge(defaultContextSpec, tc.contextSpec)
			tc.contextSpec = defaultContextSpec
			runner := &fakeSubprocessRunner{}
			tc.modules.tools = append(tc.modules.tools, makeTools(
				map[string][]string{
					"gn":    {"linux", "mac"},
					"ninja": {"linux", "mac"},
				},
			)...)
			ctx := context.Background()
			artifacts, err := buildImpl(
				ctx, runner, tc.staticSpec, tc.contextSpec, tc.modules, platform)
			if err != nil {
				if !tc.expectErr {
					t.Fatalf("Got unexpected error: %s", err)
				}
			} else if tc.expectErr {
				t.Fatal("Expected an error but got nil")
			}

			if tc.expectedArtifacts != nil {
				tc.expectedArtifacts.NinjaLogPath = filepath.Join(buildDir, ninjaLogPath)
			}
			opts := cmp.Options{
				protocmp.Transform(),
				// Ordering of the repeated artifact fields doesn't matter.
				cmpopts.SortSlices(func(a, b string) bool { return a < b }),
			}
			if diff := cmp.Diff(tc.expectedArtifacts, artifacts, opts...); diff != "" {
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

func Test_gnCheckGenerated(t *testing.T) {
	ctx := context.Background()
	runner := fakeSubprocessRunner{
		mockStdout: []byte("check error\n"),
		fail:       true,
	}
	output, err := gnCheckGenerated(ctx, &runner, "gn", t.TempDir(), t.TempDir())
	if err == nil {
		t.Fatalf("Expected gn check to fail")
	}
	if diff := cmp.Diff(string(runner.mockStdout), output); diff != "" {
		t.Errorf("Got wrong gn check output (-want +got):\n%s", diff)
	}
}
