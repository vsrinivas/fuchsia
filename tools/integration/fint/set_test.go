// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
)

type fakeSubprocessRunner struct {
	commandsRun [][]string
	run         func(cmd []string, stdout io.Writer) error
	mockStdout  []byte
	fail        bool
}

var errSubprocessFailure = errors.New("exit status 1")

func (r *fakeSubprocessRunner) Run(_ context.Context, cmd []string, options subprocess.RunOptions) error {
	if options.Stdout == nil {
		options.Stdout = os.Stdout
	}
	r.commandsRun = append(r.commandsRun, cmd)
	if r.run != nil {
		return r.run(cmd, options.Stdout)
	}
	options.Stdout.Write(r.mockStdout)
	if r.fail {
		return errSubprocessFailure
	}
	return nil
}

func TestSet(t *testing.T) {
	ctx := context.Background()

	contextSpec := &fintpb.Context{
		CheckoutDir: "/path/to/checkout",
		BuildDir:    t.TempDir(),
		ArtifactDir: "/tmp/fint-set-artifacts",
	}
	staticSpec := &fintpb.Static{
		Board:      "boards/x64.gni",
		Optimize:   fintpb.Static_DEBUG,
		Product:    "products/bringup.gni",
		TargetArch: fintpb.Static_X64,
		Variants:   []string{"asan"},
	}

	t.Run("sets artifacts metadata fields", func(t *testing.T) {
		runner := &fakeSubprocessRunner{}
		artifacts, err := setImpl(ctx, runner, staticSpec, contextSpec, "linux-x64")
		if err != nil {
			t.Fatalf("Unexpected error from setImpl: %s", err)
		}
		expectedMetadata := &fintpb.SetArtifacts_Metadata{
			Board:      staticSpec.Board,
			Optimize:   "debug",
			Product:    staticSpec.Product,
			TargetArch: "x64",
			Variants:   staticSpec.Variants,
		}

		if diff := cmp.Diff(expectedMetadata, artifacts.Metadata, protocmp.Transform()); diff != "" {
			t.Fatalf("Unexpected diff reading Static (-want +got):\n%s", diff)
		}
	})

	t.Run("propagates GN stdout to failure summary in case of failure", func(t *testing.T) {
		runner := &fakeSubprocessRunner{
			mockStdout: []byte("some stdout"),
			fail:       true,
		}
		artifacts, err := setImpl(ctx, runner, staticSpec, contextSpec, "linux-x64")
		if !errors.Is(err, errSubprocessFailure) {
			t.Fatalf("Unexpected error from setImpl: %s", err)
		}
		if artifacts.FailureSummary != string(runner.mockStdout) {
			t.Errorf("Expected setImpl to propagate GN stdout to failure summary: %q != %q", runner.mockStdout, artifacts.FailureSummary)
		}
	})

	t.Run("populates set_artifacts fields (Rust)", func(t *testing.T) {
		staticSpec := proto.Clone(staticSpec).(*fintpb.Static)
		staticSpec.UseGoma = true
		staticSpec.RustRbeEnable = true // This turns on RBE.
		runner := &fakeSubprocessRunner{
			mockStdout: []byte("some stdout"),
		}
		artifacts, err := setImpl(ctx, runner, staticSpec, contextSpec, "linux-x64")
		if err != nil {
			t.Fatalf("Unexpected error from setImpl: %s", err)
		}
		if !strings.HasPrefix(artifacts.GnTracePath, contextSpec.ArtifactDir) {
			t.Errorf("Expected setImpl to set a gn_trace_path in the artifact dir (%q) but got: %q",
				contextSpec.ArtifactDir, artifacts.GnTracePath)
		}
		if !artifacts.UseGoma {
			t.Errorf("Expected setImpl to set use_goma")
		}
		if !artifacts.EnableRbe {
			t.Errorf("Expected setImpl to set enable_rbe")
		}
	})

	t.Run("populates set_artifacts fields (C++)", func(t *testing.T) {
		staticSpec := proto.Clone(staticSpec).(*fintpb.Static)
		staticSpec.CxxRbeEnable = true // This turns on RBE.
		runner := &fakeSubprocessRunner{
			mockStdout: []byte("some stdout"),
		}
		artifacts, err := setImpl(ctx, runner, staticSpec, contextSpec, "linux-x64")
		if err != nil {
			t.Fatalf("Unexpected error from setImpl: %s", err)
		}
		if !strings.HasPrefix(artifacts.GnTracePath, contextSpec.ArtifactDir) {
			t.Errorf("Expected setImpl to set a gn_trace_path in the artifact dir (%q) but got: %q",
				contextSpec.ArtifactDir, artifacts.GnTracePath)
		}
		if !artifacts.EnableRbe {
			t.Errorf("Expected setImpl to set enable_rbe")
		}
	})

	t.Run("leaves failure summary empty in case of success", func(t *testing.T) {
		runner := &fakeSubprocessRunner{
			mockStdout: []byte("some stdout"),
		}
		artifacts, err := setImpl(ctx, runner, staticSpec, contextSpec, "linux-x64")
		if err != nil {
			t.Fatalf("Unexpected error from setImpl: %s", err)
		}
		if artifacts.FailureSummary != "" {
			t.Errorf("Expected setImpl to leave failure summary empty but got: %q", artifacts.FailureSummary)
		}
	})
}

func TestRunGen(t *testing.T) {
	ctx := context.Background()

	contextSpec := fintpb.Context{
		CheckoutDir: "/path/to/checkout",
		BuildDir:    t.TempDir(),
	}

	testCases := []struct {
		name            string
		staticSpec      *fintpb.Static
		gnTracePath     string
		expectedOptions []string
	}{
		{
			name:            "gn trace",
			gnTracePath:     "/tmp/gn_trace.json",
			expectedOptions: []string{"--tracelog=/tmp/gn_trace.json"},
		},
		{
			name: "generate IDE files",
			staticSpec: &fintpb.Static{
				IdeFiles: []string{"json", "vs"},
			},
			expectedOptions: []string{
				"--ide=json",
				"--ide=vs",
			},
		},
		{
			name: "json ide scripts",
			staticSpec: &fintpb.Static{
				JsonIdeScripts: []string{"foo.py", "bar.py"},
			},
			expectedOptions: []string{"--json-ide-script=foo.py", "--json-ide-script=bar.py"},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			if tc.staticSpec == nil {
				tc.staticSpec = &fintpb.Static{}
			}
			runner := &fakeSubprocessRunner{
				mockStdout: []byte("some stdout"),
			}

			failureSummary, err := runGen(ctx, runner, tc.staticSpec, &contextSpec, "mac-x64", tc.gnTracePath, []string{"arg1", "arg2"})
			if err != nil {
				t.Fatalf("Unexpected error from runGen: %s", err)
			}

			if string(failureSummary) != string(runner.mockStdout) {
				t.Errorf("runGen produced the wrong failure output: %q, expected %q", failureSummary, runner.mockStdout)
			}

			if len(runner.commandsRun) != 2 {
				t.Fatalf("Expected runGen to run two commands, but it ran %d", len(runner.commandsRun))
			}
			cmd := runner.commandsRun[1]
			if len(cmd) < 4 {
				t.Fatalf("runGen ran wrong command: %v", cmd)
			}

			exe, subcommand, buildDir := cmd[0], cmd[1], cmd[2]
			otherOptions := cmd[3:]
			if filepath.Base(exe) != "gn" {
				t.Errorf("runGen ran wrong GN executable: wanted basename %q, got %q", "gn", exe)
			}
			if subcommand != "gen" {
				t.Errorf("Expected runGen to run `gn gen`, but got `gn %s`", subcommand)
			}
			if buildDir != contextSpec.BuildDir {
				t.Errorf("Expected runGen to use build dir from context (%s) but got %s", contextSpec.BuildDir, buildDir)
			}
			if _, err := os.Stat(filepath.Join(contextSpec.BuildDir, "args.gn")); err != nil {
				t.Errorf("Failed to read args.gn file: %s", err)
			}
			assertSubset(t, tc.expectedOptions, otherOptions, false)
		})
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
		// Args that are not expected to be included in the return value.
		unexpectedArgs []string
		// Whether `expectedArgs` must be found in the same relative order in
		// the return value. Disabled by default to make tests less fragile.
		orderMatters bool
		// Whether we expect genArgs to return an error.
		expectErr bool
		// Relative paths to files to create in the checkout dir prior to
		// running the test case.
		checkoutFiles []string
		// Callback that will be called for each test case to add extra
		// arbitrary validation of the resulting args.
		extraChecks func(t *testing.T, args []string)
	}{
		{
			name: "minimal specs",
			expectedArgs: []string{
				`target_cpu="x64"`,
				`is_debug=true`,
			},
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
				`gcc_tool_dir="/tmp/gcc_toolchain/bin"`,
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
				`rustc_prefix="/tmp/rust_toolchain"`,
				`use_goma=true`,
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
				HostLabels:       []string{"//src:host-tests"},
			},
			expectedArgs: []string{
				`base_package_labels=["//b"]`,
				`cache_package_labels=["//c"]`,
				`universe_package_labels=["//u1","//u2"]`,
				`host_labels=["//src:host-tests"]`,
			},
		},
		{
			name: "packages with product",
			staticSpec: &fintpb.Static{
				Product:          "products/core.gni",
				BasePackages:     []string{"//b"},
				CachePackages:    []string{"//c"},
				UniversePackages: []string{"//u1", "//u2"},
				HostLabels:       []string{"//src:host-tests"},
			},
			expectedArgs: []string{
				`base_package_labels+=["//b"]`,
				`cache_package_labels+=["//c"]`,
				`universe_package_labels+=["//u1","//u2"]`,
				`host_labels+=["//src:host-tests"]`,
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
			name: "changed files and collect_coverage=true",
			contextSpec: &fintpb.Context{
				ChangedFiles: []*fintpb.Context_ChangedFile{
					{Path: "src/foo.cc"},
					{Path: "src/bar.cc"},
				},
				CollectCoverage: true,
			},
			expectedArgs: []string{
				`profile_source_files=["//src/foo.cc","//src/bar.cc"]`,
			},
		},
		{
			name: "changed files and collect_coverage=false",
			contextSpec: &fintpb.Context{
				ChangedFiles: []*fintpb.Context_ChangedFile{
					{Path: "src/foo.cc"},
					{Path: "src/bar.cc"},
				},
			},
			unexpectedArgs: []string{
				`profile_source_files=["//src/foo.cc","//src/bar.cc"]`,
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
			expectedArgs: []string{`sdk_id="789"`},
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
				`build_info_product="core"`,
				`foo="bar"`,
			},
			orderMatters: true,
		},
		{
			name: "go cache and rust cache",
			staticSpec: &fintpb.Static{
				EnableGoCache:   true,
				EnableRustCache: true,
			},
			contextSpec: &fintpb.Context{
				CacheDir: "/cache",
			},
			expectedArgs: []string{
				`gocache_dir="/cache/go_cache"`,
				`rust_incremental="/cache/rust_cache"`,
			},
		},
		{
			name: "temporary go cache",
			staticSpec: &fintpb.Static{
				UseTemporaryGoCache: true,
			},
			extraChecks: func(t *testing.T, args []string) {
				// The temporary gocache dir is dynamically generated so we
				// don't care about its exact name, we just want to make sure
				// that it is a temporary directory.
				prefix := fmt.Sprintf(`gocache_dir="%s`, os.TempDir())
				for _, arg := range args {
					if strings.HasPrefix(arg, prefix) {
						return
					}
				}
				t.Errorf("Expected an arg with prefix %s in args: %s", prefix, args)
			},
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

			args, err := genArgs(tc.staticSpec, tc.contextSpec)
			if err != nil {
				if tc.expectErr {
					return
				}
				t.Fatalf("Unexpected genArgs() error: %s", err)
			} else if tc.expectErr {
				t.Fatalf("Expected genArgs() to return an error, but got nil")
			}

			assertSubset(t, tc.expectedArgs, args, tc.orderMatters)
			if len(tc.unexpectedArgs) > 0 {
				assertNotOverlap(t, tc.unexpectedArgs, args)
			}

			if tc.extraChecks != nil {
				tc.extraChecks(t, args)
			}
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

// assertNotOverlap checks that every item in `set1` is not in `set2`.
func assertNotOverlap(t *testing.T, set1, set2 []string) {
	for _, i := range set1 {
		for _, j := range set2 {
			if i == j {
				t.Fatalf("%v and %v have one or more overlapping elements", set1, set2)
			}
		}
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
