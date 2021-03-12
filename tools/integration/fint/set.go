// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"

	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/hostplatform"
	"go.fuchsia.dev/fuchsia/tools/lib/isatty"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/runner"
)

// Set runs `gn gen` given a static and context spec. It's intended to be
// consumed as a library function.
func Set(ctx context.Context, staticSpec *fintpb.Static, contextSpec *fintpb.Context) (*fintpb.SetArtifacts, error) {
	platform, err := hostplatform.Name()
	if err != nil {
		return nil, err
	}

	runner := &runner.SubprocessRunner{}
	return runSteps(ctx, runner, staticSpec, contextSpec, platform)
}

// runSteps runs `gn gen` along with any post-processing steps, and returns a
// SetArtifacts object containing metadata produced by GN and post-processing.
func runSteps(
	ctx context.Context,
	runner subprocessRunner,
	staticSpec *fintpb.Static,
	contextSpec *fintpb.Context,
	platform string,
) (*fintpb.SetArtifacts, error) {
	if contextSpec.CheckoutDir == "" {
		return nil, fmt.Errorf("checkout_dir must be set")
	}
	if contextSpec.BuildDir == "" {
		return nil, fmt.Errorf("build_dir must be set")
	}
	genArgs, err := genArgs(staticSpec, contextSpec)
	if err != nil {
		return nil, err
	}

	artifacts := &fintpb.SetArtifacts{
		UseGoma: staticSpec.UseGoma,
		Metadata: &fintpb.SetArtifacts_Metadata{
			Board:      staticSpec.Board,
			Optimize:   strings.ToLower(staticSpec.Optimize.String()),
			Product:    staticSpec.Product,
			TargetArch: strings.ToLower(staticSpec.TargetArch.String()),
			Variants:   staticSpec.Variants,
		},
	}
	if contextSpec.ArtifactDir != "" {
		artifacts.GnTracePath = filepath.Join(contextSpec.ArtifactDir, "gn_trace.json")
	}
	genStdout, err := runGen(ctx, runner, staticSpec, contextSpec, platform, artifacts.GnTracePath, genArgs)
	if err != nil {
		artifacts.FailureSummary = genStdout
		return artifacts, err
	}
	// Only run build graph analysis if the result will be emitted via
	// artifacts, and if we actually care about checking the result.
	if contextSpec.ArtifactDir != "" && staticSpec.SkipIfUnaffected {
		var changedFiles []string
		for _, f := range contextSpec.ChangedFiles {
			changedFiles = append(changedFiles, f.Path)
		}
		sb, err := shouldBuild(ctx, runner, contextSpec.BuildDir, contextSpec.CheckoutDir, platform, changedFiles)
		if err != nil {
			return artifacts, err
		}
		artifacts.SkipBuild = !sb
	}
	return artifacts, err
}

func runGen(
	ctx context.Context,
	runner subprocessRunner,
	staticSpec *fintpb.Static,
	contextSpec *fintpb.Context,
	platform string,
	gnTracePath string,
	args []string,
) (genStdout string, err error) {
	gn := thirdPartyPrebuilt(contextSpec.CheckoutDir, platform, "gn")

	formattedArgs := gnFormat(ctx, gn, runner, args)

	genCmd := []string{
		gn,
		"gen",
		contextSpec.BuildDir,
		"--check=system",
		"--fail-on-unused-args",
		// If --ninja-executable is set, GN runs `ninja -t restat build.ninja`
		// after generating the ninja files, updating the cached modified
		// timestamps of files. This avoids extra regens when running ninja
		// repeatedly under some circumstances.
		fmt.Sprintf("--ninja-executable=%s", thirdPartyPrebuilt(contextSpec.CheckoutDir, platform, "ninja")),
	}

	if isatty.IsTerminal() {
		genCmd = append(genCmd, "--color")
	}
	if gnTracePath != "" {
		genCmd = append(genCmd, fmt.Sprintf("--tracelog=%s", gnTracePath))
	}
	if staticSpec.GenerateCompdb {
		genCmd = append(genCmd, "--export-compile-commands")
	}
	for _, f := range staticSpec.IdeFiles {
		genCmd = append(genCmd, fmt.Sprintf("--ide=%s", f))
	}
	for _, s := range staticSpec.JsonIdeScripts {
		genCmd = append(genCmd, fmt.Sprintf("--json-ide-script=%s", s))
	}

	genCmd = append(genCmd, fmt.Sprintf("--args=\n%s", formattedArgs))

	// When `gn gen` fails, it outputs a brief helpful error message to stdout.
	var stdoutBuf bytes.Buffer
	if err := runner.Run(ctx, genCmd, io.MultiWriter(&stdoutBuf, os.Stdout), os.Stderr); err != nil {
		return stdoutBuf.String(), fmt.Errorf("error running gn gen: %w", err)
	}
	return stdoutBuf.String(), nil
}

func genArgs(staticSpec *fintpb.Static, contextSpec *fintpb.Context) ([]string, error) {
	// GN variables to set via args (mapping from variable name to value).
	vars := make(map[string]interface{})
	// GN list variables to which we want to append via args (mapping from
	// variable name to list of values to append).
	appends := make(map[string][]string)
	// GN targets to import.
	var imports []string

	if staticSpec.TargetArch == fintpb.Static_ARCH_UNSPECIFIED {
		// Board files declare `target_cpu` so it's not necessary to set
		// `target_cpu` as long as we have a board file.
		if staticSpec.Board == "" {
			return nil, fmt.Errorf("target_arch must be set if board is not")
		}
	} else {
		vars["target_cpu"] = strings.ToLower(staticSpec.TargetArch.String())
	}

	if staticSpec.Optimize == fintpb.Static_OPTIMIZE_UNSPECIFIED {
		return nil, fmt.Errorf("optimize is unspecified or invalid")
	}
	vars["is_debug"] = staticSpec.Optimize == fintpb.Static_DEBUG

	if contextSpec.ClangToolchainDir != "" {
		if staticSpec.UseGoma {
			return nil, fmt.Errorf("goma is not supported for builds using a custom clang toolchain")
		}
		vars["clang_prefix"] = filepath.Join(contextSpec.ClangToolchainDir, "bin")
	}
	if contextSpec.GccToolchainDir != "" {
		if staticSpec.UseGoma {
			return nil, fmt.Errorf("goma is not supported for builds using a custom gcc toolchain")
		}
		vars["gcc_tool_dir"] = filepath.Join(contextSpec.GccToolchainDir, "bin")
	}
	if contextSpec.RustToolchainDir != "" {
		vars["rustc_prefix"] = filepath.Join(contextSpec.RustToolchainDir, "bin")
	}

	vars["use_goma"] = staticSpec.UseGoma

	if staticSpec.Product != "" {
		basename := filepath.Base(staticSpec.Product)
		vars["build_info_product"] = strings.Split(basename, ".")[0]
		imports = append(imports, staticSpec.Product)
	}

	if staticSpec.Board != "" {
		basename := filepath.Base(staticSpec.Board)
		vars["build_info_board"] = strings.Split(basename, ".")[0]
		imports = append(imports, staticSpec.Board)
	}

	if contextSpec.SdkId != "" {
		vars["sdk_id"] = contextSpec.SdkId
		vars["build_sdk_archives"] = true
	}

	if contextSpec.ReleaseVersion != "" {
		vars["build_info_version"] = contextSpec.ReleaseVersion
	}

	if staticSpec.TestDurationsFile != "" {
		testDurationsFile := staticSpec.TestDurationsFile
		exists, err := osmisc.FileExists(filepath.Join(contextSpec.CheckoutDir, testDurationsFile))
		if err != nil {
			return nil, fmt.Errorf("failed to check if TestDurationsFile exists: %w", err)
		}
		if !exists {
			testDurationsFile = staticSpec.DefaultTestDurationsFile
		}
		vars["test_durations_file"] = testDurationsFile
	}

	for varName, values := range map[string][]string{
		"base_package_labels":     staticSpec.BasePackages,
		"cache_package_labels":    staticSpec.CachePackages,
		"universe_package_labels": staticSpec.UniversePackages,
		"host_labels":             staticSpec.HostLabels,
	} {
		if len(values) == 0 {
			continue
		}
		// If product is set, append to the corresponding list variable instead
		// of overwriting it to avoid overwriting any packages set in the
		// imported product file.
		if staticSpec.Product == "" {
			vars[varName] = values
		} else {
			appends[varName] = values
		}
	}

	if len(staticSpec.Variants) != 0 {
		vars["select_variant"] = staticSpec.Variants
		if contains(staticSpec.Variants, "thinlto") {
			vars["thinlto_cache_dir"] = filepath.Join(contextSpec.CacheDir, "thinlto")
		}
		if contains(staticSpec.Variants, "profile") && contextSpec.CollectCoverage && len(contextSpec.ChangedFiles) > 0 {
			var profileSourceFiles []string
			for _, file := range contextSpec.ChangedFiles {
				profileSourceFiles = append(profileSourceFiles, fmt.Sprintf("//%s", file.Path))
			}
			vars["profile_source_files"] = profileSourceFiles
		}
	}

	if staticSpec.EnableGoCache {
		vars["gocache_dir"] = filepath.Join(contextSpec.CacheDir, "go_cache")
	}

	if staticSpec.EnableRustCache {
		vars["rust_incremental"] = filepath.Join(contextSpec.CacheDir, "rust_cache")
	}

	var normalArgs []string
	var importArgs []string
	for _, arg := range staticSpec.GnArgs {
		if strings.HasPrefix(arg, "import(") {
			importArgs = append(importArgs, arg)
		} else {
			normalArgs = append(normalArgs, arg)
		}
	}

	for k, v := range vars {
		normalArgs = append(normalArgs, fmt.Sprintf("%s=%s", k, toGNValue(v)))
	}
	for k, v := range appends {
		normalArgs = append(normalArgs, fmt.Sprintf("%s+=%s", k, toGNValue(v)))
	}
	sort.Strings(normalArgs)

	for _, p := range imports {
		importArgs = append(importArgs, fmt.Sprintf(`import("//%s")`, p))
	}
	sort.Strings(importArgs)

	var finalArgs []string

	// Ensure that imports come before args that set or modify variables, as
	// otherwise the imported files might blindly redefine variables set or
	// modified by other arguments.
	finalArgs = append(finalArgs, importArgs...)
	finalArgs = append(finalArgs, normalArgs...)
	return finalArgs, nil
}

// gnFormat makes a best-effort attempt to format the input arguments using `gn
// format` so that the output will be more readable in case of any errors like
// unknown variables. If formatting fails (e.g. due to a syntax error) we'll
// just return the unformatted args and let `gn gen` return an error; otherwise
// we'd need duplicated error handling code to handle both syntax errors from
// `gn format` and non-syntax errors from `gn gen`.
func gnFormat(ctx context.Context, gn string, runner subprocessRunner, args []string) string {
	var output bytes.Buffer
	unformatted := strings.Join(args, "\n")
	input := strings.NewReader(unformatted)
	if err := runner.RunWithStdin(ctx, []string{gn, "format", "--stdin"}, &output, nil, input); err != nil {
		return unformatted
	}
	return output.String()
}

// toGNValue converts a Go value to a string representation of the corresponding
// GN value by inspecting the Go value's type. This makes the logic to set GN
// args more readable and less error-prone, with no need for patterns like
// fmt.Sprintf(`"%s"`) repeated everywhere.
//
// For example:
// - toGNValue(true) => `true`
// - toGNValue("foo") => `"foo"` (a string containing literal double-quotes)
// - toGNValue([]string{"foo", "bar"}) => `["foo","bar"]`
func toGNValue(x interface{}) string {
	switch val := x.(type) {
	case bool:
		return fmt.Sprintf("%v", val)
	case string:
		// Apply double-quotes to strings, but not to GN scopes like
		// {variant="asan-fuzzer" target_type=["fuzzed_executable"]}
		if strings.HasPrefix(val, "{") && strings.HasSuffix(val, "}") {
			return val
		}
		return fmt.Sprintf(`"%s"`, val)
	case []string:
		var values []string
		for _, element := range val {
			values = append(values, toGNValue(element))
		}
		return fmt.Sprintf("[%s]", strings.Join(values, ","))
	default:
		panic(fmt.Sprintf("unsupported arg value type %T", val))
	}
}

func contains(items []string, target string) bool {
	for _, item := range items {
		if item == target {
			return true
		}
	}
	return false
}
