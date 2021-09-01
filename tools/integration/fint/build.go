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
	"io/ioutil"
	"math"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"google.golang.org/protobuf/types/known/structpb"

	"go.fuchsia.dev/fuchsia/tools/build"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/hostplatform"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
)

const (
	// Name of the QEMU kernel image as it appears in images.json.
	qemuKernelImageName = "qemu-kernel"

	// ninjaLogPath is the path to the main ninja log relative to the build directory.
	ninjaLogPath = ".ninja_log"
)

var (
	// Names of images that are used for running on QEMU.
	qemuImageNames = []string{qemuKernelImageName, "zircon-a"}

	// Values of the "device_type" zbi_tests.json field that are emulators.
	emulatorDeviceTypes = []string{"QEMU", "AEMU"}

	// Extra targets to build when building images. Needed for size checks and tracking.
	extraTargetsForImages = []string{
		"build/images:record_filesystem_sizes",
		"build/images:system_snapshot",
	}
)

type buildModules interface {
	Archives() []build.Archive
	GeneratedSources() []string
	Images() []build.Image
	PrebuiltBinarySets() []build.PrebuiltBinarySet
	TestSpecs() []build.TestSpec
	Tools() build.Tools
	ZBITests() []build.ZBITest
}

// Build runs `ninja` given a static and context spec. It's intended to be
// consumed as a library function.
// TODO(olivernewman): Add tests for this function.
func Build(ctx context.Context, staticSpec *fintpb.Static, contextSpec *fintpb.Context) (*fintpb.BuildArtifacts, error) {
	platform, err := hostplatform.Name()
	if err != nil {
		return nil, err
	}

	runner := ninjaRunner{
		runner:    &subprocess.Runner{},
		ninjaPath: thirdPartyPrebuilt(contextSpec.CheckoutDir, platform, "ninja"),
		buildDir:  contextSpec.BuildDir,
		jobCount:  int(contextSpec.GomaJobCount),
	}

	// Let Ninja determine whether it's necessary to run `gn gen` again first,
	// because later steps will consume GN-generated files.
	if err := ninjaGNGen(ctx, runner); err != nil {
		return nil, err
	}

	modules, err := build.NewModules(contextSpec.BuildDir)
	if err != nil {
		return nil, err
	}

	targets, artifacts, err := constructNinjaTargets(modules, staticSpec, platform)
	if err != nil {
		return nil, err
	}
	artifacts.BuiltTargets = targets
	// The ninja log is generated automatically by Ninja and its path is
	// constant relative to the build directory.
	artifacts.NinjaLogPath = filepath.Join(contextSpec.BuildDir, ninjaLogPath)
	// Initialize the map, otherwise it will be nil and attempts to set keys
	// will fail.
	artifacts.LogFiles = make(map[string]string)

	if staticSpec.Incremental && !staticSpec.NoCleandead {
		// If we're building incrementally, we need to clean out any stale files
		// in the build directory.
		if err := ninjaCleanDead(ctx, runner); err != nil {
			return nil, err
		}
	}

	ninjaStartTime := time.Now()
	var ninjaErr error
	if contextSpec.ArtifactDir == "" {
		// If we don't care about collecting artifacts, which is generally the
		// case when running locally, then there's no need to parse the Ninja
		// stdout to get the failure message. So let Ninja print directly to
		// stdout, so it will nicely buffer output when running in a terminal
		// instead of printing each log on a new line.
		ninjaErr = runner.run(ctx, targets, os.Stdout, os.Stderr)
	} else {
		var explainSink io.Writer
		if staticSpec.Incremental {
			f, err := ioutil.TempFile(contextSpec.ArtifactDir, "")
			if err != nil {
				return nil, err
			}
			defer f.Close()
			artifacts.LogFiles["explain_output.txt"] = f.Name()
			explainSink = f
		}
		artifacts.FailureSummary, ninjaErr = runNinja(
			ctx,
			runner,
			targets,
			// Add -d explain to incremental builds.
			staticSpec.Incremental,
			explainSink,
		)
	}
	ninjaDuration := time.Since(ninjaStartTime)
	artifacts.NinjaDurationSeconds = int32(math.Round(ninjaDuration.Seconds()))

	// As an optimization, we only bother collecting graph and compdb data if we
	// have a way to return it to the caller. We want to collect this data even
	// when the build failed.
	if contextSpec.ArtifactDir != "" {
		artifacts.NinjaGraphPath, err = ninjaGraph(ctx, runner, targets)
		if err != nil {
			return nil, err
		}
		artifacts.NinjaCompdbPath, err = ninjaCompdb(ctx, runner)
		if err != nil {
			return nil, err
		}
	}

	if ninjaErr != nil {
		return artifacts, ninjaErr
	}

	gn := thirdPartyPrebuilt(contextSpec.CheckoutDir, platform, "gn")
	if output, err := gnCheckGenerated(ctx, runner.runner, gn, contextSpec.CheckoutDir, contextSpec.BuildDir); err != nil {
		artifacts.FailureSummary = output
		return artifacts, err
	}

	// saveLogs writes the given set of logs to files in the artifact directory,
	// and adds each path to the output artifacts.
	saveLogs := func(logs map[string]string) error {
		if contextSpec.ArtifactDir == "" {
			return nil
		}
		for name, contents := range logs {
			f, err := ioutil.TempFile(contextSpec.ArtifactDir, url.QueryEscape(name))
			if err != nil {
				return err
			}
			defer f.Close()
			if _, err := f.WriteString(contents); err != nil {
				return fmt.Errorf("failed to write log file %q: %w", name, err)
			}
			artifacts.LogFiles[name] = f.Name()
		}
		return nil
	}

	if !contextSpec.SkipNinjaNoopCheck {
		noop, logs, err := checkNinjaNoop(ctx, runner, targets, hostplatform.IsMac())
		if err != nil {
			return nil, err
		}
		if err := saveLogs(logs); err != nil {
			return nil, err
		}
		if !noop {
			summaryLines := []string{
				"Ninja build did not converge to no-op.",
				"See: https://fuchsia.dev/fuchsia-src/development/build/ninja_no_op",
			}
			if hostplatform.IsMac() {
				summaryLines = append(
					summaryLines,
					"If this failure is specific to Mac, confirm that it's not related to fxbug.dev/61784.",
				)
			}
			artifacts.FailureSummary = strings.Join(
				summaryLines,
				"\n",
			)
			return artifacts, fmt.Errorf("ninja build did not converge to no-op")
		}
	}

	// TODO(olivernewman): Figure out a way to skip this analysis when the
	// caller doesn't care about running tests, or just wants to run all tests
	// in the same way regardless of any build graph analysis. In the meantime
	// it's not the end of the world to do this analysis unnecessarily, since it
	// only takes ~10 seconds and we do use the results most of the time,
	// including on all the slowest infra builders.
	if contextSpec.ArtifactDir != "" && len(contextSpec.ChangedFiles) > 0 && len(modules.TestSpecs()) > 0 {
		var tests []build.Test
		for _, t := range modules.TestSpecs() {
			tests = append(tests, t.Test)
		}
		var affectedFiles []string
		for _, f := range contextSpec.ChangedFiles {
			absPath := filepath.Join(contextSpec.CheckoutDir, f.Path)
			affectedFiles = append(affectedFiles, absPath)
		}
		result, err := affectedTestsNoWork(ctx, runner, tests, affectedFiles, targets)
		if err != nil {
			return nil, err
		}
		if err := saveLogs(result.logs); err != nil {
			return nil, err
		}
		artifacts.AffectedTests = result.affectedTests
		artifacts.BuildNotAffected = result.noWork
	}

	return artifacts, nil
}

func constructNinjaTargets(
	modules buildModules,
	staticSpec *fintpb.Static,
	platform string,
) ([]string, *fintpb.BuildArtifacts, error) {
	var targets []string
	var artifacts fintpb.BuildArtifacts

	if staticSpec.IncludeImages {
		targets = append(targets, extraTargetsForImages...)

		for _, image := range modules.Images() {
			if isTestingImage(image, staticSpec.Pave) {
				targets = append(targets, image.Path)
				imageStruct, err := toStructPB(image)
				if err != nil {
					return nil, nil, err
				}
				artifacts.BuiltImages = append(artifacts.BuiltImages, imageStruct)
			}
		}

		// TODO(fxbug.dev/43568): Remove once it is always false, and move
		// "build/images:updates" into `extraTargetsForImages`.
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
		} else {
			targets = append(targets, "build/images:updates")
		}
	}

	if staticSpec.IncludeGeneratedSources {
		targets = append(targets, modules.GeneratedSources()...)
	}

	if staticSpec.IncludeHostTests {
		for _, testSpec := range modules.TestSpecs() {
			if testSpec.OS != "fuchsia" {
				targets = append(targets, testSpec.Path)
			}
		}
	}

	if staticSpec.IncludeZbiTests {
		artifacts.ZbiTestQemuKernelImages = map[string]*structpb.Struct{}
		for _, zbiTest := range modules.ZBITests() {
			targets = append(targets, zbiTest.Path)
			for _, dt := range zbiTest.DeviceTypes {
				if !contains(emulatorDeviceTypes, dt) {
					continue
				}
				// A ZBI test may specify another image to be run as the QEMU
				// kernel. Ensure that it is built.
				img, err := getQEMUKernelImage(zbiTest, modules)
				if err != nil {
					return nil, nil, err
				}
				imgPB, err := toStructPB(img)
				if err != nil {
					return nil, nil, err
				}
				artifacts.ZbiTestQemuKernelImages[zbiTest.Name] = imgPB
				targets = append(targets, img.Path)
				break
			}
		}

		var zedbootImages []*structpb.Struct
		for _, img := range modules.Images() {
			if len(img.PaveZedbootArgs) == 0 {
				continue
			}
			// TODO(fxbug.dev/68977): Remove when we can find a way to boot from the
			// same slot we pave zedboot to.
			if staticSpec.TargetArch == fintpb.Static_ARM64 && contains(img.PaveZedbootArgs, "--zircona") {
				img.PaveZedbootArgs = append(img.PaveZedbootArgs, "--zirconb")
			}
			imgPB, err := toStructPB(img)
			if err != nil {
				return nil, nil, err
			}
			zedbootImages = append(zedbootImages, imgPB)
			targets = append(targets, img.Path)
		}
		if len(zedbootImages) == 0 {
			return nil, nil, fmt.Errorf("missing zedboot pave images")
		}
		artifacts.BuiltZedbootImages = zedbootImages
	}

	if staticSpec.IncludePrebuiltBinaryManifests {
		for _, manifest := range modules.PrebuiltBinarySets() {
			targets = append(targets, manifest.Manifest)
		}
	}

	if len(staticSpec.Tools) != 0 {
		// We only support specifying tools for the current platform. Tools
		// needed for other platforms can be included in the build indirectly
		// via higher-level targets.
		availableTools := modules.Tools().AsMap(platform)
		for _, toolName := range staticSpec.Tools {
			tool, ok := availableTools[toolName]
			if !ok {
				return nil, nil, fmt.Errorf("tool %q with platform %q does not exist", toolName, platform)
			}
			targets = append(targets, tool.Path)
		}
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
		return true
	// Allow-list a specific set of zbi images that are used for testing.
	case image.Type == "zbi" && image.Name == "overnet":
		return true
	default:
		return false
	}
}

// getQEMUKernelImage iterates through images.json to find the QEMU kernel image
// for the given ZBI test.
func getQEMUKernelImage(zbiTest build.ZBITest, modules buildModules) (build.Image, error) {
	var images []build.Image
	for _, img := range modules.Images() {
		include := false
		if zbiTest.QEMUKernelLabel != "" {
			include = img.Label == zbiTest.QEMUKernelLabel
		} else {
			include = img.Name == qemuKernelImageName
		}
		if include {
			images = append(images, img)
		}
	}
	// If 'qemu_kernel_label' is specified, precisely one image with that
	// label must exist; else, precisely one with the name of "qemu-kernel"
	// must exist.
	if len(images) == 0 {
		return build.Image{}, fmt.Errorf("no QEMU kernel match found for zbi test %q", zbiTest.Name)
	} else if len(images) > 1 {
		return build.Image{}, fmt.Errorf("multiple QEMU kernel matches found for zbi test %q", zbiTest.Name)
	}
	img := images[0]
	img.Name = qemuKernelImageName
	img.Type = "kernel"
	return img, nil
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

// removeDuplicates rearranges and re-slices the given slice in-place, returning
// a slice containing only the unique elements of the original slice.
func removeDuplicates(slice []string) []string {
	sort.Strings(slice)

	var numUnique int
	var previous string
	for _, str := range slice {
		if previous == "" || str != previous {
			slice[numUnique] = str
			numUnique++
		}
		previous = str
	}

	return slice[:numUnique]
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
