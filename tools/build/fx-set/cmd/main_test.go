// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	"github.com/golang/protobuf/proto"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"google.golang.org/protobuf/testing/protocmp"
)

func TestParseArgsAndEnv(t *testing.T) {
	testCases := []struct {
		name      string
		args      []string
		expected  setArgs
		expectErr bool
	}{
		{
			name:      "missing PRODUCT.BOARD",
			args:      []string{},
			expectErr: true,
		},
		{
			name: "valid PRODUCT.BOARD",
			args: []string{"core.x64"},
			expected: setArgs{
				product: "core",
				board:   "x64",
			},
		},
		{
			name:      "invalid PRODUCT.BOARD",
			args:      []string{"corex64"},
			expectErr: true,
		},
		{
			name:      "multiple PRODUCT.BOARD args",
			args:      []string{"core.x64", "bringup.arm64"},
			expectErr: true,
		},
		{
			name: "verbose",
			args: []string{"--verbose", "core.x64"},
			expected: setArgs{
				product: "core",
				board:   "x64",
				verbose: true,
			},
		},
		{
			name: "universe packages",
			args: []string{"core.x64", "--with", "u1,u2", "--with", "u3,u4"},
			expected: setArgs{
				product:          "core",
				board:            "x64",
				universePackages: []string{"u1", "u2", "u3", "u4"},
			},
		},
		{
			name: "base packages",
			args: []string{"core.x64", "--with-base", "b1,b2", "--with-base", "b3,b4"},
			expected: setArgs{
				product:      "core",
				board:        "x64",
				basePackages: []string{"b1", "b2", "b3", "b4"},
			},
		},
		{
			name: "cache packages",
			args: []string{"core.x64", "--with-cache", "c1,c2", "--with-cache", "c3,c4"},
			expected: setArgs{
				product:       "core",
				board:         "x64",
				cachePackages: []string{"c1", "c2", "c3", "c4"},
			},
		},
		{
			name: "host labels",
			args: []string{"core.x64", "--with-host", "h1,h2", "--with-host", "h3,h4"},
			expected: setArgs{
				product:    "core",
				board:      "x64",
				hostLabels: []string{"h1", "h2", "h3", "h4"},
			},
		},
		{
			name: "variants",
			args: []string{"core.x64", "--variant", "kasan,profile", "--variant", "ubsan"},
			expected: setArgs{
				product:  "core",
				board:    "x64",
				variants: []string{"kasan", "profile", "ubsan"},
			},
		},
		{
			name: "gn args",
			args: []string{"core.x64", "--args", `foo=["bar", "baz"]`, "--args", "x=5"},
			expected: setArgs{
				product: "core",
				board:   "x64",
				// --args values shouldn't be split at commas, since commas can
				// be part of the args themselves.
				gnArgs: []string{`foo=["bar", "baz"]`, "x=5"},
			},
		},
		{
			name: "release",
			args: []string{"core.x64", "--release"},
			expected: setArgs{
				product:   "core",
				board:     "x64",
				isRelease: true,
			},
		},
		{
			name: "fint params path",
			args: []string{"--fint-params-path", "foo.fint.textproto"},
			expected: setArgs{
				fintParamsPath: "foo.fint.textproto",
			},
		},
		{
			name:      "rejects --goma and --no-goma",
			args:      []string{"core.x64", "--goma", "--no-goma"},
			expectErr: true,
		},
		{
			name:      "rejects --ccache and --no-ccache",
			args:      []string{"core.x64", "--ccache", "--no-ccache"},
			expectErr: true,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			checkoutDir := t.TempDir()
			tc.expected.checkoutDir = checkoutDir

			cmd, err := parseArgsAndEnv(tc.args, map[string]string{checkoutDirEnvVar: checkoutDir})
			if err != nil {
				if !tc.expectErr {
					t.Fatalf("Parse args error: %s", err)
				}
				return
			} else if tc.expectErr {
				t.Fatalf("Expected parse args error but parsing succeeded")
			}

			opts := []cmp.Option{cmpopts.EquateEmpty(), cmp.AllowUnexported(setArgs{})}
			if diff := cmp.Diff(&tc.expected, cmd, opts...); diff != "" {
				t.Fatalf("Unexpected arg parse result (-want +got):\n%s", diff)
			}
		})
	}
}

type fakeSubprocessRunner struct {
	fail bool
}

func (r fakeSubprocessRunner) Run(context.Context, []string, io.Writer, io.Writer) error {
	if r.fail {
		return &exec.ExitError{}
	}
	return nil
}

func TestConstructStaticSpec(t *testing.T) {
	testCases := []struct {
		name     string
		args     *setArgs
		runner   fakeSubprocessRunner
		expected *fintpb.Static
	}{
		{
			name: "basic",
			args: &setArgs{
				board:            "arm64",
				product:          "bringup",
				isRelease:        true,
				basePackages:     []string{"base"},
				cachePackages:    []string{"cache"},
				universePackages: []string{"universe"},
				hostLabels:       []string{"host"},
				variants:         []string{"variant"},
				gnArgs:           []string{"args"},
				noGoma:           true,
			},
			expected: &fintpb.Static{
				Board:            "boards/arm64.gni",
				Product:          "products/bringup.gni",
				Optimize:         fintpb.Static_RELEASE,
				BasePackages:     []string{"base"},
				CachePackages:    []string{"cache"},
				UniversePackages: []string{"universe"},
				HostLabels:       []string{"host"},
				Variants:         []string{"variant"},
				GnArgs:           []string{"args"},
				UseGoma:          false,
			},
		},
		{
			name: "ccache enabled",
			args: &setArgs{
				useCcache: true,
			},
			expected: &fintpb.Static{
				GnArgs:  []string{"use_ccache=true"},
				UseGoma: false,
			},
		},
		{
			name: "goma enabled",
			args: &setArgs{
				useGoma: true,
			},
			expected: &fintpb.Static{
				UseGoma: true,
			},
		},
		{
			name: "goma enabled by default",
			args: &setArgs{},
			expected: &fintpb.Static{
				UseGoma: true,
			},
		},
		{
			name:   "goma disabled if goma auth fails",
			args:   &setArgs{},
			runner: fakeSubprocessRunner{fail: true},
			expected: &fintpb.Static{
				UseGoma: false,
			},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			ctx := context.Background()
			if tc.args.board == "" {
				tc.args.board = "x64"
			}
			if tc.args.product == "" {
				tc.args.product = "core"
			}

			expected := &fintpb.Static{
				Board:    "boards/x64.gni",
				Product:  "products/core.gni",
				Optimize: fintpb.Static_DEBUG,
			}
			proto.Merge(expected, tc.expected)

			checkoutDir := t.TempDir()
			createFile(t, checkoutDir, expected.Board)
			createFile(t, checkoutDir, expected.Product)

			got, err := constructStaticSpec(ctx, tc.runner, checkoutDir, tc.args)
			if err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(expected, got, protocmp.Transform()); diff != "" {
				t.Fatalf("Static spec from args is wrong (-want +got):\n%s", diff)
			}
		})
	}
}

func TestFindGNIFile(t *testing.T) {
	t.Run("finds file in root boards directory", func(t *testing.T) {
		checkoutDir := t.TempDir()
		path := filepath.Join("boards", "core.gni")
		createFile(t, checkoutDir, path)

		wantPath := "boards/core.gni"
		gotPath, err := findGNIFile(checkoutDir, "boards", "core")
		if err != nil {
			t.Fatal(err)
		}
		if wantPath != gotPath {
			t.Errorf("findGNIFile returned wrong path: want %s, got %s", wantPath, gotPath)
		}
	})

	t.Run("checks vendor directories first", func(t *testing.T) {
		checkoutDir := t.TempDir()
		path := filepath.Join("vendor", "foo", "boards", "core.gni")
		createFile(t, checkoutDir, path)
		// Even if a matching file exists in the root "//boards" directory, we
		// should prefer the file from the vendor directory.
		createFile(t, checkoutDir, "boards", "core.gni")

		gotPath, err := findGNIFile(checkoutDir, "boards", "core")
		if err != nil {
			t.Fatal(err)
		}
		if path != gotPath {
			t.Errorf("findGNIFile returned wrong path: want %s, got %s", path, gotPath)
		}
	})

	t.Run("returns an error if file doesn't exist", func(t *testing.T) {
		checkoutDir := t.TempDir()
		createFile(t, checkoutDir, "boards", "core.gni")

		if path, err := findGNIFile(checkoutDir, "boards", "doesnotexist"); err == nil {
			t.Fatalf("Expected findGNIFile to fail given a nonexistent board but got path: %s", path)
		}
	})
}

func createFile(t *testing.T, pathParts ...string) {
	t.Helper()

	path := filepath.Join(pathParts...)
	f, err := osmisc.CreateFile(path)
	if err != nil {
		t.Fatal(err)
	}
	f.Close()

	t.Cleanup(func() { os.Remove(path) })
}
