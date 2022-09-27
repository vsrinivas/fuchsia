// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/structpb"

	"go.fuchsia.dev/fuchsia/tools/build"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/hostplatform"
	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
)

const (
	// Name of the QEMU kernel image as it appears in images.json.
	qemuKernelImageName = "qemu-kernel"

	// ninjaLogPath is the path to the main ninja log relative to the build directory.
	ninjaLogPath = ".ninja_log"
	// ninjaDepsPath is the path to the log of ninja deps relative to the build
	// directory.
	ninjaDepsPath = ".ninja_deps"

	// Name of the directory within the build directory that will contain clang
	// crash reports. This name is configured by the `crash_diagnostics_dir` GN
	// arg in //build/config/clang/crash_diagnostics.gni.
	clangCrashReportsDirName = "clang-crashreports"

	// Name of the directory within the build directory that will contain traces
	// with files accesses after a build. By default only traces for targets with
	// unexpected accesses are persisted after a build.
	//
	// This name is configured in //build/config/BUILDCONFIG.gn.
	fileAccessTracesDirName = ".traces"
	// Name suffix of file access traces.
	//
	// This name is configured in //build/config/BUILDCONFIG.gn.
	fileAccessTracesSuffix = "_trace.txt"
)

var (
	// Path to a file within the build dir which, if its timestamp is updated,
	// will cause the build system to rebuild all nonhermetic build actions.
	rebuildNonHermeticActionsPath = []string{"force_nonhermetic_rebuild"}

	// Names of images that are used for running on QEMU.
	qemuImageNames = []string{qemuKernelImageName, "zircon-a"}

	// Extra targets to build when building images. Needed for size checks and tracking.
	extraTargetsForImages = []string{
		"build/images:record_filesystem_sizes",
	}
)

type buildModules interface {
	Archives() []build.Archive
	ClippyTargets() []build.ClippyTarget
	GeneratedSources() []string
	Images() []build.Image
	PrebuiltBinarySets() []build.PrebuiltBinarySet
	TestSpecs() []build.TestSpec
	Tools() build.Tools
}

// Build runs `ninja` given a static and context spec. It's intended to be
// consumed as a library function.
func Build(ctx context.Context, staticSpec *fintpb.Static, contextSpec *fintpb.Context) (*fintpb.BuildArtifacts, error) {
	platform, err := hostplatform.Name()
	if err != nil {
		return nil, err
	}
	modules, err := build.NewModules(contextSpec.BuildDir)
	if err != nil {
		return nil, err
	}
	artifacts, err := buildImpl(ctx, &subprocess.Runner{}, staticSpec, contextSpec, modules, platform)
	if err != nil && artifacts != nil && artifacts.FailureSummary == "" {
		// Fall back to using the error text as the failure summary if the
		// failure summary is unset. It's better than failing without emitting
		// any information.
		artifacts.FailureSummary = err.Error()
	}
	return artifacts, err
}

// buildImpl contains the business logic of `fint build`, extracted into a more
// easily testable layer.
func buildImpl(
	ctx context.Context,
	runner subprocessRunner,
	staticSpec *fintpb.Static,
	contextSpec *fintpb.Context,
	modules buildModules,
	platform string,
) (*fintpb.BuildArtifacts, error) {
	if staticSpec.Incremental {
		// If we're building incrementally, we need to rebuild all nonhermetic
		// actions. This is done by touching a particular file in the build
		// directory.
		path := filepath.Join(append([]string{contextSpec.BuildDir}, rebuildNonHermeticActionsPath...)...)
		if err := runner.Run(ctx, []string{"touch", filepath.Join(path)}, os.Stdout, os.Stderr); err != nil {
			return nil, err
		}
	}

	artifacts := &fintpb.BuildArtifacts{}

	targets, targetArtifacts, err := constructNinjaTargets(modules, staticSpec, contextSpec, platform)
	if err != nil {
		return artifacts, err
	}
	proto.Merge(artifacts, targetArtifacts)

	// Initialize maps, otherwise they will be nil and attempts to set keys will
	// fail.
	artifacts.LogFiles = make(map[string]string)

	ninjaPath, err := toolAbsPath(modules, contextSpec.BuildDir, platform, "ninja")
	if err != nil {
		return artifacts, err
	}
	r := ninjaRunner{
		runner:    runner,
		ninjaPath: ninjaPath,
		buildDir:  contextSpec.BuildDir,
		jobCount:  int(contextSpec.GomaJobCount),
	}

	ninjaStartTime := time.Now()
	var ninjaErr error
	if contextSpec.ArtifactDir == "" {
		// If we don't care about collecting artifacts, which is generally the
		// case when running locally, then there's no need to parse the Ninja
		// stdout to get the failure message. So let Ninja print directly to
		// stdout, so it will nicely buffer output when running in a terminal
		// instead of printing each log on a new line.
		ninjaErr = r.run(ctx, append(staticSpec.NinjaArgs, targets...), os.Stdout, os.Stderr)
	} else {
		var explainSink io.Writer
		if staticSpec.Incremental {
			f, err := os.Create(filepath.Join(contextSpec.ArtifactDir, "explain_output.txt"))
			if err != nil {
				return artifacts, err
			}
			defer f.Close()
			artifacts.LogFiles["explain_output.txt"] = f.Name()
			explainSink = f
		}
		artifacts.FailureSummary, artifacts.NinjaActionMetrics, ninjaErr = runNinja(
			ctx,
			r,
			staticSpec.NinjaArgs,
			targets,
			// Add -d explain to incremental builds.
			staticSpec.Incremental,
			explainSink,
		)
	}
	ninjaDuration := time.Since(ninjaStartTime)
	artifacts.NinjaDurationSeconds = int32(math.Round(ninjaDuration.Seconds()))

	// The ninja log is generated automatically by Ninja and its path is
	// constant relative to the build directory.
	artifacts.NinjaLogPath = filepath.Join(contextSpec.BuildDir, ninjaLogPath)

	// As an optimization, we only bother collecting graph and compdb data if we
	// have a way to return it to the caller. We want to collect this data even
	// when the build failed.
	if contextSpec.ArtifactDir != "" {
		graph := filepath.Join(contextSpec.ArtifactDir, "ninja-graph.dot")
		if err := ninjaGraph(ctx, r, targets, graph); err != nil {
			if ninjaErr == nil {
				return artifacts, err
			}
		} else {
			artifacts.NinjaGraphPath = graph
		}

		compdb := filepath.Join(contextSpec.ArtifactDir, "compile-commands.json")
		if err := ninjaCompdb(ctx, r, compdb); err != nil {
			if ninjaErr == nil {
				return artifacts, err
			}
		} else {
			artifacts.NinjaCompdbPath = compdb
		}
	}

	saveDebugFiles := func(paths []string) error {
		for _, path := range paths {
			rel, err := filepath.Rel(contextSpec.BuildDir, path)
			if err != nil {
				return err
			}
			artifacts.DebugFiles = append(artifacts.DebugFiles, &fintpb.DebugFile{
				Path:       path,
				UploadDest: filepath.ToSlash(rel),
			})
		}
		return nil
	}

	if ninjaErr != nil {
		if contextSpec.ArtifactDir != "" {
			crashReportFiles, err := collectClangCrashReports(contextSpec.BuildDir)
			if err != nil {
				return artifacts, err
			}
			if err := saveDebugFiles(crashReportFiles); err != nil {
				return artifacts, err
			}

			traces, err := collectFileAccessTraces(contextSpec.BuildDir)
			if err != nil {
				return artifacts, fmt.Errorf("collecting file access traces: %w", err)
			}
			if err := saveDebugFiles(traces); err != nil {
				return artifacts, fmt.Errorf("writing file access traces to debug files: %w", err)
			}
		}
		return artifacts, fmt.Errorf("build failed, see ninja output for details: %w", ninjaErr)
	}

	gnPath, err := toolAbsPath(modules, contextSpec.BuildDir, platform, "gn")
	if err != nil {
		return artifacts, err
	}
	if output, err := gnCheckGenerated(ctx, runner, gnPath, contextSpec.CheckoutDir, contextSpec.BuildDir); err != nil {
		artifacts.FailureSummary = output
		return artifacts, err
	}

	if !contextSpec.SkipNinjaNoopCheck {
		noop, logs, err := checkNinjaNoop(ctx, r, targets, hostplatform.IsMac(platform))
		if err != nil {
			return artifacts, err
		}
		if err := saveLogs(contextSpec.ArtifactDir, artifacts, logs); err != nil {
			return artifacts, err
		}
		if !noop {
			// Upload additional log files to help with debugging.
			artifacts.DebugFiles = append(artifacts.DebugFiles, []*fintpb.DebugFile{
				{
					Path:       filepath.Join(contextSpec.BuildDir, ninjaLogPath),
					UploadDest: ninjaLogPath,
				},
				{
					Path:       filepath.Join(contextSpec.BuildDir, ninjaDepsPath),
					UploadDest: ninjaDepsPath,
				},
			}...)

			if contextSpec.ArtifactDir != "" {
				buildDirManifest := filepath.Join(contextSpec.ArtifactDir, "build_dir_manifest.json")
				if err := writeBuildDirManifest(contextSpec.BuildDir, buildDirManifest); err != nil {
					logger.Warningf(ctx, "Failed to create manifest of build dir contents: %s", err)
				} else {
					artifacts.DebugFiles = append(artifacts.DebugFiles, &fintpb.DebugFile{
						Path:       buildDirManifest,
						UploadDest: "build_dir_manifest.json",
					})
				}
			}

			artifacts.FailureSummary = ninjaNoopFailureMessage(platform)
			return artifacts, fmt.Errorf("ninja build did not converge to no-op")
		}
	}

	// TODO(olivernewman): Figure out a way to skip this analysis when the
	// caller doesn't care about running tests, or just wants to run all tests
	// in the same way regardless of any build graph analysis. In the meantime
	// it's not the end of the world to do this analysis unnecessarily, since it
	// only takes ~10 seconds and we do use the results most of the time,
	// including on all the slowest infra builders.
	newArtifacts, err := affectedImpl(ctx, runner, staticSpec, contextSpec, modules, platform, targets)
	if err != nil {
		return artifacts, err
	}
	proto.Merge(artifacts, newArtifacts)
	return artifacts, nil
}

// collectClangCrashReports finds reproducer scripts for clang crashes in the
// build directory and returns a list of absolute paths to those files.
func collectClangCrashReports(buildDir string) ([]string, error) {
	crashReportsDir := filepath.Join(buildDir, clangCrashReportsDirName)
	if isDir, err := osmisc.IsDir(crashReportsDir); err != nil || !isDir {
		return nil, err
	}

	reproducers, err := filepath.Glob(filepath.Join(crashReportsDir, "*.sh"))
	if err != nil {
		return nil, err
	}

	var outputs []string
	for _, rep := range reproducers {
		noExt := strings.TrimSuffix(rep, filepath.Ext(rep))
		files, err := filepath.Glob(noExt + ".*")
		if err != nil {
			return nil, err
		}
		outputs = append(outputs, files...)
	}

	return outputs, nil
}

// collectFileAccessTraces finds access traces in the build directory, and
// returns paths to them. Note this function returns absolute paths because they
// are expected by `debug_files`.
func collectFileAccessTraces(buildDir string) ([]string, error) {
	var traces []string
	err := filepath.WalkDir(filepath.Join(buildDir, fileAccessTracesDirName), func(path string, info fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if !info.IsDir() && strings.HasSuffix(path, fileAccessTracesSuffix) {
			traces = append(traces, path)
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	return traces, nil
}

func ninjaNoopFailureMessage(platform string) string {
	summaryLines := []string{
		"Ninja build did not converge to no-op.",
		"See: https://fuchsia.dev/fuchsia-src/development/build/ninja_no_op",
	}
	if hostplatform.IsMac(platform) {
		summaryLines = append(
			summaryLines,
			"If this failure is specific to Mac, confirm that it's not related to fxbug.dev/61784.",
		)
	}
	return strings.Join(summaryLines, "\n")
}

// constructNinjaTargets determines which targets to build based on the static
// spec fields and the contents of the build API files. It emits a
// BuildArtifacts protobuf with only a subset of fields set that should be
// merged into an existing BuildArtifacts protobuf struct by the caller, since
// this is more clean than taking a BuildArtifacts pointer as input and
// modifying it.
func constructNinjaTargets(
	modules buildModules,
	staticSpec *fintpb.Static,
	contextSpec *fintpb.Context,
	platform string,
) ([]string, *fintpb.BuildArtifacts, error) {
	var targets []string
	var artifacts fintpb.BuildArtifacts

	if staticSpec.IncludeDefaultNinjaTarget {
		targets = append(targets, ":default")
	} else {

		// "//:host" is a dep of "//:default"
		if staticSpec.IncludeHostTests {
			for _, testSpec := range modules.TestSpecs() {
				if testSpec.OS != "fuchsia" {
					targets = append(targets, testSpec.Path)
				}
			}
		}
	}

	if staticSpec.IncludeImages {
		targets = append(targets, extraTargetsForImages...)

		for _, image := range modules.Images() {
			if isTestingImage(image, staticSpec.Pave) {

				// All deps of "//:images" are deps of "//:default", so the
				// targets don't need to be specified
				if !staticSpec.IncludeDefaultNinjaTarget {
					targets = append(targets, image.Path)
				}

				// However, the artifacts need to be added.
				imageStruct, err := toStructPB(image)
				if err != nil {
					return nil, nil, err
				}
				artifacts.BuiltImages = append(artifacts.BuiltImages, imageStruct)
			}
		}

		// TODO(fxbug.dev/43568): Remove once it is always false, and move
		// "build/images/updates" into `extraTargetsForImages`.
		if staticSpec.IncludeArchives {
			archivesToBuild := []string{
				"archive",  // Images and scripts for paving/netbooting.
				"packages", // Package metadata, blobs, and tools.
			}
			for _, archive := range modules.Archives() {
				if contains(archivesToBuild, archive.Name) && archive.Type == "tgz" {
					targets = append(targets, archive.Path)
					archiveStruct, err := toStructPB(archive)
					if err != nil {
						return nil, nil, err
					}
					artifacts.BuiltArchives = append(artifacts.BuiltArchives, archiveStruct)
				}
			}
		}
	}

	if staticSpec.IncludeGeneratedSources {
		targets = append(targets, modules.GeneratedSources()...)
	}

	if staticSpec.IncludePrebuiltBinaryManifests {
		for _, manifest := range modules.PrebuiltBinarySets() {
			targets = append(targets, manifest.Manifest)
		}
	}

	lintTargets, err := chooseLintTargets(modules, staticSpec, contextSpec)
	if err != nil {
		return nil, nil, err
	}
	targets = append(targets, lintTargets...)

	// We only support specifying tools for the current platform. Tools
	// needed for other platforms can be included in the build indirectly
	// via higher-level targets.
	for _, tool := range staticSpec.Tools {
		path, err := modules.Tools().LookupPath(platform, tool)
		if err != nil {
			return nil, nil, err
		}
		targets = append(targets, path)
	}

	targets = append(targets, staticSpec.NinjaTargets...)
	return removeDuplicates(targets), &artifacts, nil
}

// isTestingImage determines whether an image is necessary for testing Fuchsia.
//
// The `pave` argument indicates whether images will be used for paving (as
// opposed to netbooting).
//
// If an image is used in paving or netbooting, its manifest entry will specify
// what flags to pass to the bootserver when doing so.
func isTestingImage(image build.Image, pave bool) bool {
	switch {
	case len(image.PaveZedbootArgs) != 0: // Used by catalyst.
		return true
	case pave && len(image.PaveArgs) != 0: // Used for paving.
		return true
	case !pave && len(image.NetbootArgs) != 0: // Used for netboot.
		return true
	case contains(qemuImageNames, image.Name): // Used for QEMU.
		return true
	case image.Name == "uefi-disk": // Used for GCE.
		return true
	case image.Type == "script":
		// In order for a user to provision without Zedboot the scripts are
		// needed too, so we want to include them such that artifactory can
		// upload them. This covers scripts like "pave.sh", "flash.sh", etc.
		if pave && strings.Contains(image.Name, "netboot") {
			// If we're paving then we shouldn't build any netboot scripts,
			// because they would pull netboot images into the build graph that
			// take a while to build and that we don't actually need.
			return false
		}
		return true
	// Allow-list a specific set of zbi images that are used for testing.
	case image.Type == "zbi" && image.Name == "overnet":
		return true
	default:
		return false
	}
}

func chooseLintTargets(
	modules buildModules,
	staticSpec *fintpb.Static,
	contextSpec *fintpb.Context,
) ([]string, error) {
	if staticSpec.IncludeLintTargets == fintpb.Static_NO_LINT_TARGETS {
		return nil, nil
	}

	changed := make(map[string]bool)
	for _, f := range contextSpec.ChangedFiles {
		changed[f.Path] = true
	}

	shouldInclude := func(sources []string) (bool, error) {
		if staticSpec.IncludeLintTargets == fintpb.Static_ALL_LINT_TARGETS {
			return true, nil
		} else if staticSpec.IncludeLintTargets != fintpb.Static_AFFECTED_LINT_TARGETS {
			return false, fmt.Errorf("unknown include_lint_targets value: %s", staticSpec.IncludeLintTargets)
		}
		for _, source := range sources {
			checkoutPath, err := filepath.Rel(
				contextSpec.CheckoutDir,
				filepath.Clean(filepath.Join(contextSpec.BuildDir, source)))
			if err != nil {
				return false, err
			}
			if changed[filepath.ToSlash(checkoutPath)] {
				return true, nil
			}
		}
		return false, nil
	}

	var targets []string
	for _, clippy := range modules.ClippyTargets() {
		include, err := shouldInclude(clippy.Sources)
		if err != nil {
			return nil, err
		}
		if include {
			targets = append(targets, clippy.Output)
		}
	}
	return targets, nil
}

// toolAbsPath returns the absolute path to a tool specified in tool_paths.json.
//
// Note that not all tools in tool_paths.json can be assumed to be present in
// the build directory prior to `fint build` running because only a subset of
// them are prebuilts, and the rest are built from source.
func toolAbsPath(modules buildModules, buildDir, platform, tool string) (string, error) {
	path, err := modules.Tools().LookupPath(platform, tool)
	if err != nil {
		return "", err
	}
	return filepath.Abs(filepath.Join(buildDir, path))
}

// toStructPB converts a Go struct to a Struct protobuf. Unfortunately, short of
// using some complicated `reflect` logic, the only way to do this conversion is
// by using JSON as an intermediate format.
func toStructPB(s interface{}) (*structpb.Struct, error) {
	j, err := json.Marshal(s)
	if err != nil {
		return nil, err
	}
	var m map[string]interface{}
	if err := json.Unmarshal(j, &m); err != nil {
		return nil, err
	}
	return structpb.NewStruct(m)
}

// removeDuplicates returns a slice containing only the unique elements of the
// original slice.
func removeDuplicates(slice []string) []string {
	uniq := make(map[string]struct{})
	for _, str := range slice {
		uniq[str] = struct{}{}
	}
	var deduped []string
	for str := range uniq {
		deduped = append(deduped, str)
	}
	// Sort to make the output deterministic for unit tests.
	sort.Strings(deduped)
	return deduped
}

// gnCheckGenerated runs `gn check` against a build directory that's already had
// `ninja` run, to check generated files that weren't available for checking at
// the time we ran `fint set`.
//
// It returns the stdout of the subprocess.
func gnCheckGenerated(ctx context.Context, r subprocessRunner, gn, checkoutDir, buildDir string) (string, error) {
	cmd := []string{
		gn,
		"check",
		buildDir,
		fmt.Sprintf("--root=%s", checkoutDir),
		"--check-generated",
		"--check-system",
	}
	var stdoutBuf bytes.Buffer
	if err := r.Run(ctx, cmd, io.MultiWriter(&stdoutBuf, os.Stdout), os.Stderr); err != nil {
		return stdoutBuf.String(), fmt.Errorf("error running `gn check`: %w", err)
	}
	return stdoutBuf.String(), nil
}

type buildDirManifestElement struct {
	// Path is the path to the file, relative to the build directory root.
	Path string `json:"path"`
	// SyminkTarget is only set if this file is a symlink, and specifies the
	// path that the symlink points to.
	SymlinkTarget string `json:"symlink_target,omitempty"`
	// ModTimeMilliseconds is the modification time of the file, in the form of
	// a Unix millisecond timestamp for consistency with the timestamp format
	// that ninja uses.
	ModTimeMilliseconds float64 `json:"mtime"`
}

// writeBuildDirManifest constructs a manifest of all the files in the build
// directory and writes it as JSON to `outputPath`. This manifest is useful for
// debugging incremental build failures.
func writeBuildDirManifest(buildDir string, outputPath string) error {
	// Empty slice instead of nil so it gets rendered as an empty JSON array
	// instead of null.
	ret := []buildDirManifestElement{}

	if err := filepath.WalkDir(buildDir, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() {
			return nil
		}

		info, err := d.Info()
		if err != nil {
			return err
		}

		relpath, err := filepath.Rel(buildDir, path)
		if err != nil {
			return err
		}

		var symlinkTarget string
		if d.Type()&fs.ModeSymlink != 0 {
			symlinkTarget, err = os.Readlink(path)
			if err != nil {
				return err
			}
		}

		ret = append(ret, buildDirManifestElement{
			Path:          filepath.ToSlash(relpath),
			SymlinkTarget: filepath.ToSlash(symlinkTarget),
			// Note that for symlinks this is the mtime of the symlink itself,
			// rather than the mtime of the target of the symlink.
			ModTimeMilliseconds: float64(info.ModTime().UnixMicro()) / float64(time.Millisecond/time.Microsecond),
		})
		return nil
	}); err != nil {
		return err
	}

	return jsonutil.WriteToFile(outputPath, ret)
}
