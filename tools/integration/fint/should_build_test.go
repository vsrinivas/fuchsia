// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"
	"errors"
	"fmt"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"

	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
)

// gnAnalyzeRunner conforms to the subprocessRunner interface but is
// specifically intended for testing functions that are only intended to use it
// to make a single `gn analyze` call.
type gnAnalyzeRunner struct {
	output gnAnalyzeOutput

	// Remaining fields are to be set by `Run()`.

	// The exact command that was run.
	cmd []string

	// The JSON input that was passed to the gn analyze command.
	input gnAnalyzeInput
}

func (r *gnAnalyzeRunner) Run(_ context.Context, cmd []string, _ subprocess.RunOptions) error {
	if len(r.cmd) != 0 {
		return errors.New("Run() can only be called once per runner")
	}
	r.cmd = cmd

	// Relying on index is somewhat fragile but easier than the alternatives.
	inputPath := cmd[3]
	outputPath := cmd[4]

	if err := jsonutil.ReadFromFile(inputPath, &r.input); err != nil {
		return fmt.Errorf("failed to read GN analyze input: %w", err)
	}

	return jsonutil.WriteToFile(outputPath, r.output)
}

func TestShouldBuild(t *testing.T) {
	ctx := context.Background()

	testCases := []struct {
		name       string
		files      []string
		mockOutput gnAnalyzeOutput

		// Whether we expect `gn analyze` to be run in this test case.
		expectNoCommand bool

		// Whether this test case should determine that the build graph is
		// affected.
		expectShouldBuild bool

		// Whether this test case is expected to cause the function to return an
		// error.
		expectErr bool
	}{
		{
			name:              "unknown file type",
			files:             []string{"foo.invalid"},
			expectNoCommand:   true,
			expectShouldBuild: true,
		},
		{
			name: "affected",
			mockOutput: gnAnalyzeOutput{
				Status: buildGraphAffectedStatus,
			},
			expectShouldBuild: true,
		},
		{
			name: "unaffected",
			mockOutput: gnAnalyzeOutput{
				Status: buildGraphNotAffectedStatus,
			},
			expectShouldBuild: false,
		},
		{
			name: "unknown status",
			mockOutput: gnAnalyzeOutput{
				Status: unknownAffectedStatus,
			},
			expectShouldBuild: true,
		},
		{
			name: "invalid status",
			mockOutput: gnAnalyzeOutput{
				Status: "invalid",
			},
			expectErr: true,
		},
		{
			name:              "skipped",
			files:             []string{"third_party/go/foo.go", "foo/bar.go"},
			expectNoCommand:   true,
			expectShouldBuild: true,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			if len(tc.files) == 0 {
				tc.files = []string{"foo/bar.cc", "baz.py"}
			}

			runner := &gnAnalyzeRunner{
				output: tc.mockOutput,
			}
			platform := "linux-x64"
			buildDir := filepath.Join(t.TempDir(), "build")
			checkoutDir := filepath.Join(t.TempDir(), "checkout")

			gotShouldBuild, err := shouldBuild(ctx, runner, buildDir, checkoutDir, platform, tc.files)
			if tc.expectErr && err == nil {
				t.Fatalf("Expected error but analysis succeeded")
			} else if !tc.expectErr && err != nil {
				t.Fatalf("Unexpected analysis error: %s", err)
			}

			if gotShouldBuild != tc.expectShouldBuild {
				t.Errorf("Wrong affected result: wanted %v, got %v", tc.expectShouldBuild, gotShouldBuild)
			}

			if tc.expectNoCommand {
				if len(runner.cmd) != 0 {
					t.Fatalf("Expected no command to be run, but this command was run: %v", runner.cmd)
				}
				// The remaining assertions deal only with the case where gn
				// analyze is expected to be run.
				return
			}

			expectedCmd := []string{thirdPartyPrebuilt(checkoutDir, platform, "gn"), "analyze", buildDir}
			if diff := cmp.Diff(runner.cmd[:len(expectedCmd)], expectedCmd); diff != "" {
				t.Fatalf("Wrong gn command run (-want +got):\n%s", diff)
			}

			if diff := cmp.Diff(formatFilePaths(tc.files), runner.input.Files); diff != "" {
				t.Fatalf("gn analyze received the wrong list of files (-want +got):\n%s", diff)
			}
		})
	}
}

func TestCanAnalyzeFiles(t *testing.T) {
	ctx := context.Background()

	testCases := []struct {
		name               string
		files              []string
		expectedCanAnalyze bool
	}{
		{
			name:               "no files",
			files:              []string{},
			expectedCanAnalyze: true,
		},
		{
			name:               "all file types supported",
			files:              []string{"foo.py", "baz.go", "abc/def.cc", "abc/OWNERS"},
			expectedCanAnalyze: true,
		},
		{
			name:               "some file types unsupported",
			files:              []string{"foo.py", "bar/baz.invalid", "noextension"},
			expectedCanAnalyze: false,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			canAnalyze := canAnalyzeFiles(ctx, tc.files)
			if canAnalyze != tc.expectedCanAnalyze {
				t.Errorf("Wrong canAnalyze result: wanted %v, got %v", tc.expectedCanAnalyze, canAnalyze)
			}
		})
	}
}
