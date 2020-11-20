// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"github.com/golang/protobuf/proto"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/cmd/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
)

type fakeSubprocessRunner struct {
	commandsRun [][]string
}

func (r *fakeSubprocessRunner) Run(_ context.Context, cmd []string, _, _ io.Writer) error {
	r.commandsRun = append(r.commandsRun, cmd)
	return nil
}

func TestRunGen(t *testing.T) {
	ctx := context.Background()

	contextSpec := fintpb.Context{
		CheckoutDir: "/path/to/checkout",
		BuildDir:    "/path/to/out/default",
	}

	testCases := []struct {
		name            string
		staticSpec      *fintpb.Static
		expectedOptions []string
	}{
		{
			name: "default",
		},
		{
			name: "metrics collection enabled",
			staticSpec: &fintpb.Static{
				CollectMetrics: true,
			},
			expectedOptions: []string{
				fmt.Sprintf("--tracelog=%s", filepath.Join(contextSpec.BuildDir, fuchsiaGNTrace)),
			},
		},
		{
			name: "generate compdb",
			staticSpec: &fintpb.Static{
				GenerateCompdb: true,
			},
			expectedOptions: []string{
				"--export-compile-commands",
			},
		},
		{
			name: "generate IDE project",
			staticSpec: &fintpb.Static{
				GenerateIde: true,
			},
			expectedOptions: []string{
				"--ide=json",
			},
		},
	}

	for _, tc := range testCases {
		if tc.staticSpec == nil {
			tc.staticSpec = &fintpb.Static{}
		}
		runner := &fakeSubprocessRunner{}
		platform := "mac-x64"
		if err := runGen(ctx, runner, tc.staticSpec, &contextSpec, platform, []string{"arg1", "arg2"}); err != nil {
			t.Fatalf("Unexpected error from runGen: %v", err)
		}
		if len(runner.commandsRun) != 1 {
			t.Fatalf("Expected runGen to run one command, but it ran %d", len(runner.commandsRun))
		}

		cmd := runner.commandsRun[0]
		if len(cmd) < 4 {
			t.Fatalf("runGen ran wrong command: %v", cmd)
		}

		exe, subcommand, buildDir, argsOption := cmd[0], cmd[1], cmd[2], cmd[len(cmd)-1]
		otherOptions := cmd[3 : len(cmd)-1]
		// Intentionally flexible about the path within the checkout to the gn dir
		// in case it's every intentionally changed.
		expectedExePattern := regexp.MustCompile(
			fmt.Sprintf(`^%s(/\w+)+/%s/gn$`, contextSpec.CheckoutDir, platform),
		)
		if !expectedExePattern.MatchString(exe) {
			t.Errorf("runGen ran wrong GN executable: %s, expected a match of %s", exe, expectedExePattern)
		}
		if subcommand != "gen" {
			t.Errorf("Expected runGen to run `gn gen`, but got `gn %s`", subcommand)
		}
		if buildDir != contextSpec.BuildDir {
			t.Errorf("Expected runGen to use build dir from context (%s) but got %s", contextSpec.BuildDir, buildDir)
		}
		if !strings.HasPrefix(argsOption, "--args=") {
			t.Errorf("Expected runGen to pass --args as last flag")
		}
		assertSubset(t, tc.expectedOptions, otherOptions, false)
	}
}

func TestToGNValue(t *testing.T) {
	assertEqual := func(t *testing.T, actual, expected string) {
		if actual != expected {
			t.Errorf("toGNValue returned wrong value %q, expected %q", actual, expected)
		}
	}

	t.Run("boolean", func(t *testing.T) {
		assertEqual(t, toGNValue(true), "true")
		assertEqual(t, toGNValue(false), "false")
	})

	t.Run("string", func(t *testing.T) {
		assertEqual(t, toGNValue(""), `""`)
		assertEqual(t, toGNValue("foo"), `"foo"`)
	})

	t.Run("string containing GN scope", func(t *testing.T) {
		assertEqual(t, toGNValue("{x=5}"), "{x=5}")
	})

	t.Run("slice of strings", func(t *testing.T) {
		assertEqual(t, toGNValue([]string{}), `[]`)
		assertEqual(t, toGNValue([]string{"foo"}), `["foo"]`)
		assertEqual(t, toGNValue([]string{"foo", "bar"}), `["foo","bar"]`)
	})
}

func TestGenArgs(t *testing.T) {
	platform := "linux-x64"
	// Magic strings that will be replaced with the actual paths to mock
	// checkout and build dirs before making any assertions.
	checkoutDir := "$CHECKOUT_DIR"
	buildDir := "$BUILD_DIR"

	testCases := []struct {
		name        string
		contextSpec *fintpb.Context
		staticSpec  *fintpb.Static
		// Args that are expected to be included in the return value. Order does
		// not matter.
		expectedArgs []string
		// Whether `expectedArgs` must be found in the same relative order in
		// the return value. Disabled by default to make tests less fragile.
		orderMatters bool
		// Whether we expect genArgs to return an error.
		expectErr bool
		// Relative paths to files to create in the checkout dir prior to
		// running the test case.
		checkoutFiles []string
	}{
		{
			name:         "minimal specs",
			expectedArgs: []string{`target_cpu="x64"`, `is_debug=true`},
		},
		{
			name: "arm64 release",
			staticSpec: &fintpb.Static{
				TargetArch: fintpb.Static_ARM64,
				Optimize:   fintpb.Static_RELEASE,
			},
			expectedArgs: []string{`target_cpu="arm64"`, `is_debug=false`},
		},
		{
			name: "clang toolchain",
			contextSpec: &fintpb.Context{
				ClangToolchainDir: "/tmp/clang_toolchain",
			},
			expectedArgs: []string{
				`clang_prefix="/tmp/clang_toolchain/bin"`,
			},
		},
		{
			name: "clang toolchain with goma not allowed",
			contextSpec: &fintpb.Context{
				ClangToolchainDir: "/tmp/clang_toolchain",
			},
			staticSpec: &fintpb.Static{
				UseGoma: true,
			},
			expectErr: true,
		},
		{
			name: "gcc toolchain",
			contextSpec: &fintpb.Context{
				GccToolchainDir: "/tmp/gcc_toolchain",
			},
			expectedArgs: []string{
				`zircon_extra_args.gcc_tool_dir="/tmp/gcc_toolchain/bin"`,
			},
		},
		{
			name: "gcc toolchain with goma not allowed",
			contextSpec: &fintpb.Context{
				GccToolchainDir: "/tmp/gcc_toolchain",
			},
			staticSpec: &fintpb.Static{
				UseGoma: true,
			},
			expectErr: true,
		},
		{
			name: "rust toolchain with goma",
			contextSpec: &fintpb.Context{
				RustToolchainDir: "/tmp/rust_toolchain",
			},
			staticSpec: &fintpb.Static{
				UseGoma: true,
			},
			expectedArgs: []string{
				`rustc_prefix="/tmp/rust_toolchain/bin"`,
				`use_goma=true`,
				fmt.Sprintf(`goma_dir="%s/prebuilt/third_party/goma/%s"`, checkoutDir, platform),
			},
		},
		{
			name: "test durations file",
			staticSpec: &fintpb.Static{
				TestDurationsFile: "test_durations/foo.json",
			},
			checkoutFiles: []string{"test_durations/foo.json"},
			expectedArgs:  []string{`test_durations_file="test_durations/foo.json"`},
		},
		{
			name: "fall back to default test durations file",
			staticSpec: &fintpb.Static{
				TestDurationsFile:        "test_durations/foo.json",
				DefaultTestDurationsFile: "test_durations/default.json",
			},
			expectedArgs: []string{`test_durations_file="test_durations/default.json"`},
		},
		{
			name: "product",
			staticSpec: &fintpb.Static{
				Product: "products/core.gni",
			},
			expectedArgs: []string{
				`build_info_product="core"`,
				`import("//products/core.gni")`,
			},
		},
		{
			name: "board",
			staticSpec: &fintpb.Static{
				Board: "boards/x64.gni",
			},
			expectedArgs: []string{
				`build_info_board="x64"`,
				`import("//boards/x64.gni")`,
			},
		},
		{
			name: "packages",
			staticSpec: &fintpb.Static{
				BasePackages:     []string{"//b"},
				CachePackages:    []string{"//c"},
				UniversePackages: []string{"//u1", "//u2"},
			},
			expectedArgs: []string{
				`base_package_labels=["//b"]`,
				`cache_package_labels=["//c"]`,
				`universe_package_labels=["//u1","//u2"]`,
			},
		},
		{
			name: "packages with product",
			staticSpec: &fintpb.Static{
				Product:          "products/core.gni",
				BasePackages:     []string{"//b"},
				CachePackages:    []string{"//c"},
				UniversePackages: []string{"//u1", "//u2"},
			},
			expectedArgs: []string{
				`base_package_labels+=["//b"]`,
				`cache_package_labels+=["//c"]`,
				`universe_package_labels+=["//u1","//u2"]`,
			},
		},
		{
			name: "variant",
			contextSpec: &fintpb.Context{
				CacheDir: "/cache",
			},
			staticSpec: &fintpb.Static{
				Variants: []string{`thinlto`, `{variant="asan-fuzzer"}`},
			},
			expectedArgs: []string{
				`select_variant=["thinlto",{variant="asan-fuzzer"}]`,
				`thinlto_cache_dir="/cache/thinlto"`,
			},
		},
		{
			name: "release version",
			contextSpec: &fintpb.Context{
				ReleaseVersion: "1234",
			},
			expectedArgs: []string{`build_info_version="1234"`},
		},
		{
			name: "sdk id",
			contextSpec: &fintpb.Context{
				SdkId: "789",
			},
			expectedArgs: []string{`sdk_id="789"`, `build_sdk_archives=true`},
		},
		{
			name: "metrics collection enabled",
			staticSpec: &fintpb.Static{
				CollectMetrics: true,
			},
			expectedArgs: []string{
				fmt.Sprintf(`zircon_tracelog="%s"`, filepath.Join(buildDir, zirconGNTrace)),
			},
		},
		{
			name: "sorts imports first",
			staticSpec: &fintpb.Static{
				GnArgs:  []string{`foo="bar"`, `import("//foo.gni")`},
				Product: "products/core.gni",
			},
			expectedArgs: []string{
				`import("//foo.gni")`,
				`import("//products/core.gni")`,
				// This must come after all imports but before any other variables.
				`if (!defined(zircon_extra_args)) { zircon_extra_args = {} }`,
				`build_info_product="core"`,
				`foo="bar"`,
			},
			orderMatters: true,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			baseStaticSpec := &fintpb.Static{
				TargetArch: fintpb.Static_X64,
				Optimize:   fintpb.Static_DEBUG,
			}
			proto.Merge(baseStaticSpec, tc.staticSpec)
			tc.staticSpec = baseStaticSpec

			baseContextSpec := &fintpb.Context{
				CheckoutDir: filepath.Join(t.TempDir(), "checkout"),
				BuildDir:    filepath.Join(t.TempDir(), "build"),
			}
			proto.Merge(baseContextSpec, tc.contextSpec)
			tc.contextSpec = baseContextSpec

			// Replace all instances of the magic checkoutDir and builDir
			// strings with the actual path to the checkout dir, which we only
			// know at runtime.
			for i, arg := range tc.expectedArgs {
				tc.expectedArgs[i] = strings.NewReplacer(
					buildDir, tc.contextSpec.BuildDir,
					checkoutDir, tc.contextSpec.CheckoutDir,
				).Replace(arg)
			}

			for _, path := range tc.checkoutFiles {
				if f, err := osmisc.CreateFile(filepath.Join(tc.contextSpec.CheckoutDir, path)); err != nil {
					t.Fatalf("Failed to create file %s", path)
				} else {
					f.Close()
				}
			}

			args, err := genArgs(tc.staticSpec, tc.contextSpec, platform)
			if err != nil {
				if tc.expectErr {
					return
				}
				t.Fatalf("Unexpected genArgs() error: %v", err)
			} else if tc.expectErr {
				t.Fatalf("Expected genArgs() to return an error, but got nil")
			}

			assertSubset(t, tc.expectedArgs, args, tc.orderMatters)
		})
	}
}

// assertSubset checks that every item in `subset` is also in `set`. If
// `orderMatters`, then we'll also check that the relative ordering of the items
// in `subset` is the same as their relative ordering in `set`.
func assertSubset(t *testing.T, subset, set []string, orderMatters bool) {
	if isSub, msg := isSubset(subset, set, orderMatters); !isSub {
		t.Fatalf(msg)
	}
}

// isSubset is extracted from `assertSubset()` to make it possible to test this
// logic.
func isSubset(subset, set []string, orderMatters bool) (bool, string) {
	indices := make(map[string]int)
	for i, item := range set {
		if duplicateIndex, ok := indices[item]; ok {
			// Disallowing duplicates makes this function simpler, and we have
			// no need to handle duplicates.
			return false, fmt.Sprintf("Duplicate item %q found at indices %d and %d", item, duplicateIndex, i)
		}
		indices[item] = i
	}

	var previousIndex int
	for i, target := range subset {
		index, ok := indices[target]
		if !ok {
			return false, fmt.Sprintf("Expected to find %q in %+v", target, set)
		} else if orderMatters && index < previousIndex {
			return false, fmt.Sprintf("Expected %q to precede %q, but it came after", subset[i-1], target)
		}
		previousIndex = index
	}
	return true, ""
}

func TestAssertSubset(t *testing.T) {
	testCases := []struct {
		name          string
		subset        []string
		set           []string
		orderMatters  bool
		expectFailure bool
	}{
		{
			name:   "empty subset and set",
			subset: []string{},
			set:    []string{},
		},
		{
			name:   "empty subset",
			subset: []string{},
			set:    []string{"foo"},
		},
		{
			name:          "empty set",
			subset:        []string{"foo"},
			set:           []string{},
			expectFailure: true,
		},
		{
			name:   "non-empty and equal",
			subset: []string{"foo", "bar"},
			set:    []string{"foo", "bar"},
		},
		{
			name:   "non-empty strict subset",
			subset: []string{"foo"},
			set:    []string{"foo", "bar"},
		},
		{
			name:          "one item missing from set",
			subset:        []string{"foo", "bar", "baz"},
			set:           []string{"foo", "bar"},
			expectFailure: true,
		},
		{
			name:   "order does not matter",
			subset: []string{"foo", "bar"},
			set:    []string{"bar", "foo"},
		},
		{
			name:          "order matters if specified",
			subset:        []string{"foo", "bar"},
			set:           []string{"bar", "foo"},
			orderMatters:  true,
			expectFailure: true,
		},
		{
			name:          "duplicate in set",
			subset:        []string{"foo"},
			set:           []string{"foo", "foo"},
			expectFailure: true,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			isSub, msg := isSubset(tc.subset, tc.set, tc.orderMatters)
			if tc.expectFailure && isSub {
				t.Errorf("Expected assertSubset() to fail but it passed")
			} else if !tc.expectFailure && !isSub {
				t.Errorf("Expected assertSubset() to pass but it failed: %s", msg)
			}
		})
	}
}
