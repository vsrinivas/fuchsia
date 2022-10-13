// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
)

func TestParseArgsAndEnv(t *testing.T) {
	testCases := []struct {
		name      string
		args      []string
		env       map[string]string
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
			name: "fuzz-with",
			args: []string{"core.x64", "--fuzz-with", "asan,ubsan", "--fuzz-with", "kasan"},
			expected: setArgs{
				product:        "core",
				board:          "x64",
				fuzzSanitizers: []string{"asan", "ubsan", "kasan"},
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
			name:      "rejects --goma and --ccache",
			args:      []string{"core.x64", "--goma", "--ccache"},
			expectErr: true,
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
		{
			name: "honors top-level fx --dir flag",
			args: []string{"core.x64"},
			env: map[string]string{
				// fx sets this env var if the top-level --dir flag is set.
				buildDirEnvVar: "/usr/foo/fuchsia/out/foo",
			},
			expected: setArgs{
				product:  "core",
				board:    "x64",
				buildDir: "/usr/foo/fuchsia/out/foo",
			},
		},
		{
			name: "rejects --dir and --auto-dir",
			args: []string{"core.x64", "--auto-dir"},
			env: map[string]string{
				// fx sets this env var if the top-level --dir flag is set.
				buildDirEnvVar: "/usr/foo/fuchsia/out/foo",
			},
			expectErr: true,
		},
		{
			name: "auto dir",
			args: []string{"bringup.arm64", "--auto-dir"},
			expected: setArgs{
				product:  "bringup",
				board:    "arm64",
				buildDir: "out/bringup.arm64",
			},
		},
		{
			name: "auto dir with variants",
			args: []string{"core.x64", "--auto-dir", "--variant", "profile,kasan", "--variant", "ubsan"},
			expected: setArgs{
				product:  "core",
				board:    "x64",
				buildDir: "out/core.x64-profile-kasan-ubsan",
				variants: []string{"profile", "kasan", "ubsan"},
			},
		},
		{
			name: "auto dir and release",
			args: []string{"core.x64", "--auto-dir", "--variant", "foo", "--release"},
			expected: setArgs{
				product:   "core",
				board:     "x64",
				isRelease: true,
				buildDir:  "out/core.x64-foo-release",
				variants:  []string{"foo"},
			},
		},
		{
			name:      "auto dir with complex variants",
			args:      []string{"core.x64", "--auto-dir", "--variant", "asan-fuzzer/foo"},
			expectErr: true,
		},
		{
			name: "simple boolean flags",
			args: []string{"core.x64", "--netboot", "--cargo-toml-gen"},
			expected: setArgs{
				product:      "core",
				board:        "x64",
				netboot:      true,
				cargoTOMLGen: true,
			},
		},
		{
			name: "ide files",
			args: []string{"core.x64", "--ide", "json,vs"},
			expected: setArgs{
				product:  "core",
				board:    "x64",
				ideFiles: []string{"json", "vs"},
			},
		},
		{
			name: "json ide scripts",
			args: []string{"core.x64", "--json-ide-script", "//foo.py", "--json-ide-script", "//bar.py"},
			expected: setArgs{
				product:        "core",
				board:          "x64",
				jsonIDEScripts: []string{"//foo.py", "//bar.py"},
			},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			checkoutDir := t.TempDir()
			tc.expected.checkoutDir = checkoutDir

			if tc.expected.buildDir == "" {
				tc.expected.buildDir = defaultBuildDir
			}

			env := map[string]string{checkoutDirEnvVar: checkoutDir}
			for k, v := range tc.env {
				env[k] = v
			}
			cmd, err := parseArgsAndEnv(tc.args, env)
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

func (r fakeSubprocessRunner) Run(context.Context, []string, subprocess.RunOptions) error {
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
				ideFiles:         []string{"json"},
				jsonIDEScripts:   []string{"foo.py"},
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
				IdeFiles:         []string{"json"},
				JsonIdeScripts:   []string{"foo.py"},
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
		{
			name: "fuzzer variants",
			args: &setArgs{
				fuzzSanitizers: []string{"asan", "ubsan"},
			},
			expected: &fintpb.Static{
				UseGoma:  true,
				Variants: append(fuzzerVariants("asan"), fuzzerVariants("ubsan")...),
			},
		},
		{
			name: "netboot",
			args: &setArgs{
				netboot: true,
			},
			expected: &fintpb.Static{
				UseGoma: true,
				GnArgs:  []string{"enable_netboot=true"},
			},
		},
		{
			name: "cargo toml gen",
			args: &setArgs{
				basePackages: []string{"foo"},
				cargoTOMLGen: true,
			},
			expected: &fintpb.Static{
				UseGoma:      true,
				BasePackages: []string{"foo"},
				HostLabels:   []string{"//build/rust:cargo_toml_gen"},
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
				Board:             "boards/x64.gni",
				Product:           "products/core.gni",
				Optimize:          fintpb.Static_DEBUG,
				ExportRustProject: true,
			}
			proto.Merge(expected, tc.expected)

			checkoutDir := t.TempDir()
			createFile(t, checkoutDir, expected.Board)
			createFile(t, checkoutDir, expected.Product)

			fx := fxRunner{sr: tc.runner, checkoutDir: checkoutDir}
			got, err := constructStaticSpec(ctx, fx, checkoutDir, tc.args)
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

	t.Run("finds file in root products/tests directory", func(t *testing.T) {
		checkoutDir := t.TempDir()
		path := filepath.Join("products", "tests", "core.gni")
		createFile(t, checkoutDir, path)

		wantPath := "products/tests/core.gni"
		gotPath, err := findGNIFile(checkoutDir, filepath.Join("products", "tests"), "core")
		if err != nil {
			t.Fatal(err)
		}
		if wantPath != gotPath {
			t.Errorf("findGNIFile returned wrong path: want %s, got %s", wantPath, gotPath)
		}
	})

	t.Run("file in root products directory shadows root product/tests directory", func(t *testing.T) {
		checkoutDir := t.TempDir()
		path := filepath.Join("products", "core.gni")
		createFile(t, checkoutDir, path)
		shadowedPath := filepath.Join("products", "tests", "core.gni")
		createFile(t, checkoutDir, shadowedPath)

		wantPath := "products/core.gni"
		gotPath, err := findGNIFile(checkoutDir, "products", "core")
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

	t.Run("checks vendor product directories first", func(t *testing.T) {
		checkoutDir := t.TempDir()
		path := filepath.Join("vendor", "foo", "products", "tests", "core.gni")
		createFile(t, checkoutDir, path)
		// Even if a matching file exists in the root "//products/tests" directory, we
		// should prefer the file from the vendor directory.
		createFile(t, checkoutDir, "products", "tests", "core.gni")

		gotPath, err := findGNIFile(checkoutDir, filepath.Join("products", "tests"), "core")
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
