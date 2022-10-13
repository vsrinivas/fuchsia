// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/kr/pretty"
	"golang.org/x/sys/unix"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"
	"google.golang.org/protobuf/types/known/structpb"

	"go.fuchsia.dev/fuchsia/tools/build"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
)

type fakeBuildModules struct {
	archives         []build.Archive
	clippyTargets    []build.ClippyTarget
	generatedSources []string
	images           []build.Image
	pbinSets         []build.PrebuiltBinarySet
	testSpecs        []build.TestSpec
	tools            build.Tools
}

func (m fakeBuildModules) Archives() []build.Archive                     { return m.archives }
func (m fakeBuildModules) ClippyTargets() []build.ClippyTarget           { return m.clippyTargets }
func (m fakeBuildModules) GeneratedSources() []string                    { return m.generatedSources }
func (m fakeBuildModules) Images() []build.Image                         { return m.images }
func (m fakeBuildModules) PrebuiltBinarySets() []build.PrebuiltBinarySet { return m.pbinSets }
func (m fakeBuildModules) TestSpecs() []build.TestSpec                   { return m.testSpecs }
func (m fakeBuildModules) Tools() build.Tools                            { return m.tools }

func TestBuild(t *testing.T) {
	checkoutDir := t.TempDir()
	buildDir := filepath.Join(checkoutDir, "out", "default")
	artifactDir := t.TempDir()

	// The directories are shared across all test cases so they can be
	// referenced in the `testCases` table, but that means they don't get
	// cleared between sub-tests so we need to clear them explicitly.
	resetDirs := func(t *testing.T) {
		for _, dir := range []string{checkoutDir, buildDir, artifactDir} {
			if err := os.RemoveAll(dir); err != nil {
				t.Fatal(err)
			}
			if err := os.MkdirAll(dir, 0o700); err != nil {
				t.Fatal(err)
			}
		}
	}

	platform := "linux-x64"

	testCases := []struct {
		name        string
		staticSpec  *fintpb.Static
		contextSpec *fintpb.Context
		// We want to skip the ninja no-op check in most tests because it
		// requires complicated mocking, but without setting
		// `SkipNinjaNoopCheck` on every test's context spec. This effectively
		// makes `SkipNinjaNoopCheck` default to true.
		ninjaNoopCheck bool
		// Mock files to populate the build directory with.
		buildDirFiles []string
		modules       fakeBuildModules
		// Callback that is called by the fake runner whenever it starts
		// "running" a command, allowing each test to fake the result and output
		// of any subprocess.
		runnerFunc func(cmd []string, stdout io.Writer) error
		// List of regex strings, where each string corresponds to a subprocess
		// that must have been run by the runner.
		mustRun []string
		// Targets that should be built.
		expectedTargets   []string
		expectedArtifacts *fintpb.BuildArtifacts
		expectErr         bool
	}{
		{
			name:              "empty spec produces no ninja targets",
			staticSpec:        &fintpb.Static{},
			expectedArtifacts: &fintpb.BuildArtifacts{},
			mustRun:           []string{`ninja -C .*out/default$`},
		},
		{
			name:       "artifact dir set",
			staticSpec: &fintpb.Static{},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				NinjaCompdbPath: filepath.Join(artifactDir, "compile-commands.json"),
				NinjaGraphPath:  filepath.Join(artifactDir, "ninja-graph.dot"),
			},
			mustRun: []string{`ninja .*-t graph`, `ninja .*-t compdb`},
		},
		{
			name:       "affected tests",
			staticSpec: &fintpb.Static{},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
				ChangedFiles: []*fintpb.Context_ChangedFile{
					{Path: "src/foo.py"},
				},
			},
			modules: fakeBuildModules{
				testSpecs: []build.TestSpec{
					{Test: build.Test{Name: "foo"}},
				},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				NinjaCompdbPath: filepath.Join(artifactDir, "compile-commands.json"),
				NinjaGraphPath:  filepath.Join(artifactDir, "ninja-graph.dot"),
				LogFiles: map[string]string{
					"ninja dry run output": filepath.Join(artifactDir, "ninja_dry_run_output"),
				},
			},
		},
		{
			name:       "failed build with clang crash reports",
			staticSpec: &fintpb.Static{},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
			},
			// We should check for these files when the build fails and
			// reference them in the output artifacts proto.
			buildDirFiles: []string{
				filepath.Join(clangCrashReportsDirName, "foo.sh"),
				filepath.Join(clangCrashReportsDirName, "bar.sh"),
			},
			runnerFunc: func(cmd []string, _ io.Writer) error {
				return fmt.Errorf("failed to run command: %s", cmd)
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				FailureSummary: unrecognizedFailureMsg + "\n",
				DebugFiles: []*fintpb.DebugFile{
					{
						Path:       filepath.Join(buildDir, "clang-crashreports", "foo.sh"),
						UploadDest: "clang-crashreports/foo.sh",
					},
					{
						Path:       filepath.Join(buildDir, "clang-crashreports", "bar.sh"),
						UploadDest: "clang-crashreports/bar.sh",
					},
				},
			},
			expectErr: true,
		},
		{
			name:       "failed build with file access traces",
			staticSpec: &fintpb.Static{},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
			},
			buildDirFiles: []string{
				filepath.Join(".traces", "accesses_trace.txt"),
				filepath.Join(".traces", "dir", "accesses_trace.txt"),
				filepath.Join(".traces", "dir", "other_accesses_trace.txt"),
				filepath.Join(".traces", "dir", "not_a_trace"),
				filepath.Join(".traces", "nested", "dir", "accesses_trace.txt"),
				filepath.Join("not", "in", "expected", "dir", "accesses_trace.txt"),
			},
			runnerFunc: func(cmd []string, _ io.Writer) error {
				return fmt.Errorf("failed to run command: %s", cmd)
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				FailureSummary: unrecognizedFailureMsg + "\n",
				DebugFiles: []*fintpb.DebugFile{
					{
						Path:       filepath.Join(buildDir, ".traces", "accesses_trace.txt"),
						UploadDest: ".traces/accesses_trace.txt",
					},
					{
						Path:       filepath.Join(buildDir, ".traces", "dir", "accesses_trace.txt"),
						UploadDest: ".traces/dir/accesses_trace.txt",
					},
					{
						Path:       filepath.Join(buildDir, ".traces", "dir", "other_accesses_trace.txt"),
						UploadDest: ".traces/dir/other_accesses_trace.txt",
					},
					{
						Path:       filepath.Join(buildDir, ".traces", "nested", "dir", "accesses_trace.txt"),
						UploadDest: ".traces/nested/dir/accesses_trace.txt",
					},
				},
			},
			expectErr: true,
		},
		{
			name: "incremental build",
			staticSpec: &fintpb.Static{
				Incremental: true,
			},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				NinjaCompdbPath: filepath.Join(artifactDir, "compile-commands.json"),
				NinjaGraphPath:  filepath.Join(artifactDir, "ninja-graph.dot"),
				LogFiles: map[string]string{
					"explain_output.txt": filepath.Join(artifactDir, "explain_output.txt"),
				},
			},
		},
		{
			name:           "failed ninja no-op check",
			staticSpec:     &fintpb.Static{},
			ninjaNoopCheck: true,
			// We don't provide a runnerFunc, which causes the fake ninja no-op
			// check command to not print any output, so the no-op check will
			// fail because the "no work to do" string will not be present in
			// the output.
			runnerFunc: nil,
			expectErr:  true,
			expectedArtifacts: &fintpb.BuildArtifacts{
				FailureSummary: ninjaNoopFailureMessage(platform),
				DebugFiles: []*fintpb.DebugFile{
					{
						Path:       filepath.Join(buildDir, ninjaDepsPath),
						UploadDest: ninjaDepsPath,
					},
					{
						Path:       filepath.Join(buildDir, ninjaLogPath),
						UploadDest: ninjaLogPath,
					},
				},
			},
		},
		{
			name:           "passed ninja no-op check",
			staticSpec:     &fintpb.Static{},
			ninjaNoopCheck: true,
			runnerFunc: func(cmd []string, stdout io.Writer) error {
				if contains(cmd, "-n") { // -n indicates ninja dry run.
					stdout.Write([]byte(noWorkString))
				}
				return nil
			},
			expectedArtifacts: &fintpb.BuildArtifacts{},
		},
		{
			name:       "ninja graph fails",
			staticSpec: &fintpb.Static{},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
			},
			runnerFunc: func(cmd []string, stdout io.Writer) error {
				if contains(cmd, "graph") {
					return fmt.Errorf("failed to run command: %s", cmd)
				}
				return nil
			},
			expectErr:         true,
			expectedArtifacts: &fintpb.BuildArtifacts{},
		},
		{
			name:       "ninja compdb fails",
			staticSpec: &fintpb.Static{},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
			},
			runnerFunc: func(cmd []string, stdout io.Writer) error {
				if contains(cmd, "compdb") {
					return fmt.Errorf("failed to run command: %s", cmd)
				}
				return nil
			},
			expectErr: true,
			expectedArtifacts: &fintpb.BuildArtifacts{
				NinjaGraphPath: filepath.Join(artifactDir, "ninja-graph.dot"),
			},
		},
		{
			name:       "ninja graph and compdb fail after failed build",
			staticSpec: &fintpb.Static{},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
			},
			// This will cause the main ninja build to fail, along with `ninja
			// compdb` and `ninja graph`.
			runnerFunc: func(cmd []string, stdout io.Writer) error {
				if strings.HasSuffix(cmd[0], "ninja") {
					if !contains(cmd, "-t") {
						stdout.Write([]byte("[1/1](1) CXX c.o d.o\nFAILED: c.o d.o\nsomeoutput\n"))
					}
					return fmt.Errorf("failed to run command: %s", cmd)
				}
				return nil
			},
			expectErr: true,
			expectedArtifacts: &fintpb.BuildArtifacts{
				// Even if post-processing steps like `ninja graph` fail, the
				// failure summary should still attribute the failure to the
				// original ninja build error.
				FailureSummary: "[1/1](1) CXX c.o d.o\nFAILED: c.o d.o\nsomeoutput\n",
				NinjaActionMetrics: &fintpb.NinjaActionMetrics{
					InitialActions: 1,
					FinalActions:   1,
					ActionsByType: map[string]int32{
						"CXX": 1,
					},
				},
			},
			mustRun: []string{`ninja .*-t graph`, `ninja .*-t compdb`},
		},
		{
			name: "extra ad-hoc ninja targets",
			staticSpec: &fintpb.Static{
				NinjaTargets: []string{"bar", "foo"},
			},
			expectedTargets: []string{"bar", "foo"},
		},
		{
			name: "duplicate targets",
			staticSpec: &fintpb.Static{
				NinjaTargets: []string{"foo", "foo"},
			},
			expectedTargets: []string{"foo"},
		},
		{
			name: "images for testing included without default",
			staticSpec: &fintpb.Static{
				IncludeDefaultNinjaTarget: false,
				IncludeImages:             true,
			},
			modules: fakeBuildModules{
				images: []build.Image{
					{Name: qemuImageNames[0], Path: "qemu_image_path"},
					{Name: "should-be-ignored", Path: "different_path"},
				},
			},
			expectedTargets: append(extraTargetsForImages, "qemu_image_path"),
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltImages: []*structpb.Struct{
					mustStructPB(t, build.Image{Name: qemuImageNames[0], Path: "qemu_image_path"}),
				},
			},
		},
		{
			name: "images for testing not included in targets with default",
			staticSpec: &fintpb.Static{
				IncludeDefaultNinjaTarget: true,
				IncludeImages:             true,
			},
			modules: fakeBuildModules{
				images: []build.Image{
					{Name: qemuImageNames[0], Path: "qemu_image_path"},
					{Name: "should-be-ignored", Path: "different_path"},
				},
			},
			expectedTargets: append(extraTargetsForImages, ":default"),
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
			expectedTargets: append(extraTargetsForImages, "foo.sh"),
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltImages: []*structpb.Struct{
					mustStructPB(t, build.Image{Name: "foo", Path: "foo.sh", Type: "script"}),
				},
			},
		},
		{
			name: "default ninja target included",
			staticSpec: &fintpb.Static{
				IncludeDefaultNinjaTarget: true,
			},
			expectedTargets: []string{":default"},
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
			name: "all lint targets",
			staticSpec: &fintpb.Static{
				IncludeLintTargets: fintpb.Static_ALL_LINT_TARGETS,
			},
			modules: fakeBuildModules{
				clippyTargets: []build.ClippyTarget{
					{
						Output: "gen/src/foo.clippy",
						Sources: []string{
							"../../src/foo/x.rs",
							"../../src/foo/y.rs",
						},
					},
					{
						Output: "gen/src/bar.clippy",
						Sources: []string{
							"../../src/bar/x.rs",
							"../../src/bar/y.rs",
						},
					},
				},
			},
			expectedTargets: []string{"gen/src/foo.clippy", "gen/src/bar.clippy"},
		},
		{
			name: "affected lint targets",
			staticSpec: &fintpb.Static{
				IncludeLintTargets: fintpb.Static_AFFECTED_LINT_TARGETS,
			},
			contextSpec: &fintpb.Context{
				ChangedFiles: []*fintpb.Context_ChangedFile{
					{Path: "src/foo/x.rs"},
					{Path: "src/foo/y.rs"},
				},
			},
			modules: fakeBuildModules{
				clippyTargets: []build.ClippyTarget{
					{
						Output: "gen/src/foo.clippy",
						Sources: []string{
							"../../src/foo/x.rs",
							"../../src/foo/y.rs",
						},
					},
					{
						Output: "gen/src/bar.clippy",
						Sources: []string{
							"../../src/bar/x.rs",
							"../../src/bar/y.rs",
						},
					},
				},
			},
			expectedTargets: []string{"gen/src/foo.clippy"},
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
			resetDirs(t)

			for _, relpath := range tc.buildDirFiles {
				createEmptyFile(t, filepath.Join(buildDir, relpath))
			}

			defaultContextSpec := &fintpb.Context{
				SkipNinjaNoopCheck: !tc.ninjaNoopCheck,
				CheckoutDir:        checkoutDir,
				BuildDir:           buildDir,
			}
			proto.Merge(defaultContextSpec, tc.contextSpec)
			tc.contextSpec = defaultContextSpec
			runner := &fakeSubprocessRunner{run: tc.runnerFunc}
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

			ninjaTargets := findNinjaTargets(runner.commandsRun)

			sortSlicesOpt := cmpopts.SortSlices(func(a, b string) bool { return a < b })

			if diff := cmp.Diff(tc.expectedTargets, ninjaTargets, sortSlicesOpt, cmpopts.EquateEmpty()); diff != "" {
				t.Errorf("Wrong ninja targets build (-want +got):\n%s", diff)
			}

			if tc.expectedArtifacts == nil {
				tc.expectedArtifacts = &fintpb.BuildArtifacts{}
			}
			if len(runner.commandsRun) > 0 {
				// If preprocessing fails before we run any subprocesses, then
				// NinjaLogPath will not be set.
				tc.expectedArtifacts.NinjaLogPath = filepath.Join(buildDir, ninjaLogPath)
			}
			opts := cmp.Options{
				protocmp.Transform(),
				// Ordering of the repeated artifact fields doesn't matter.
				sortSlicesOpt,
				protocmp.SortRepeated(func(a, b *fintpb.DebugFile) bool { return a.Path < b.Path }),
			}
			if diff := cmp.Diff(tc.expectedArtifacts, artifacts, opts...); diff != "" {
				t.Errorf("Got wrong artifacts (-want +got):\n%s", diff)
			}

			for _, s := range tc.mustRun {
				re, err := regexp.Compile(s)
				if err != nil {
					t.Fatal(err)
				}
				found := false
				for _, cmd := range runner.commandsRun {
					if re.MatchString(strings.Join(cmd, " ")) {
						found = true
						break
					}
				}
				if !found {
					t.Errorf("No command was run matching %q. Commands run: %s", s, pretty.Sprint(runner.commandsRun))
				}
			}
		})
	}
}

// findNinjaTargets is a hacky utility to return a list of built ninja targets,
// given a list of command line invocations that is assumed to contain a ninja
// invocation.
//
// It returns an empty slice if it fails to find a ninja invocation in the list
// of commands.
func findNinjaTargets(cmds [][]string) []string {
	for _, cmd := range cmds {
		// Ignore non-ninja commands and ninja tool invocations.
		if filepath.Base(cmd[0]) != "ninja" || contains(cmd, "-t") {
			continue
		}
		// Skip over each `-flag value` pair until we reach the list of targets.
		for i := 1; i < len(cmd); i += 2 {
			if !strings.HasPrefix(cmd[i], "-") {
				// We've reached the start of the list of targets, assumed to
				// come after all flags.
				return cmd[i:]
			}
		}
		// Found a ninja command that specified zero targets, implying the
		// default target.
		return nil
	}
	// Failed to find a full-build ninja command (likely because something
	// failed before ninja was run). We don't bother to distinguish between this
	// case and the zero-targets case because tests separately check if a
	// failure occurred.
	return nil
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

func createEmptyFile(t *testing.T, path string) {
	t.Helper()

	f, err := osmisc.CreateFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
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

func Test_writeBuildDirManifest(t *testing.T) {
	buildDir := t.TempDir()

	regularFile := filepath.Join(buildDir, "dir", "foo")
	createEmptyFile(t, regularFile)

	symlink := filepath.Join(buildDir, "bar-link")
	// The symlink target doesn't actually exist, which is fine - we should be
	// able to construct the manifest anyway, including the invalid symlink.
	if err := os.Symlink(filepath.Join("..", "..", "bar"), symlink); err != nil {
		t.Fatal(err)
	}

	for _, path := range []string{regularFile, symlink} {
		tv := unix.Timeval{Sec: 123, Usec: 456789}
		// Use unix-specific Lutimes instead of os.Chtimes to ensure we update
		// the mtime of the symlink itself rather than its target.
		if err := unix.Lutimes(path, []unix.Timeval{tv, tv}); err != nil {
			t.Fatal(err)
		}
	}

	want := []buildDirManifestElement{
		{
			Path:                "dir/foo",
			ModTimeMilliseconds: 123456.789,
		},
		{
			Path:                "bar-link",
			SymlinkTarget:       "../../bar",
			ModTimeMilliseconds: 123456.789,
		},
	}

	outputPath := filepath.Join(t.TempDir(), "output.json")
	if err := writeBuildDirManifest(buildDir, outputPath); err != nil {
		t.Fatal(err)
	}

	var got []buildDirManifestElement
	if err := jsonutil.ReadFromFile(outputPath, &got); err != nil {
		t.Fatalf("Failed to read output file: %s", err)
	}

	opts := []cmp.Option{
		cmpopts.SortSlices(func(x, y buildDirManifestElement) bool {
			return x.Path < y.Path
		}),
		cmpopts.EquateEmpty(),
	}

	if diff := cmp.Diff(want, got, opts...); diff != "" {
		t.Errorf("Build dir manifest is wrong (-want +got):\n%s", diff)
	}
}
