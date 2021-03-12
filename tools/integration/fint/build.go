// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"
	"os"
	"sort"

	"go.fuchsia.dev/fuchsia/tools/build"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/hostplatform"
	"go.fuchsia.dev/fuchsia/tools/lib/runner"
)

var (
	qemuImageNames = []string{"qemu-kernel", "zircon-a"}

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
	ZBITests() []build.ZBITest
}

// Build runs `ninja` given a static and context spec. It's intended to be
// consumed as a library function.
func Build(ctx context.Context, staticSpec *fintpb.Static, contextSpec *fintpb.Context) error {
	platform, err := hostplatform.Name()
	if err != nil {
		return err
	}

	modules, err := build.NewModules(contextSpec.BuildDir)
	if err != nil {
		return err
	}

	runner := &runner.SubprocessRunner{}
	targets := constructNinjaTargets(modules, staticSpec)

	ninjaPath := thirdPartyPrebuilt(contextSpec.CheckoutDir, platform, "ninja")
	cmd := []string{ninjaPath, "-C", contextSpec.BuildDir}
	cmd = append(cmd, targets...)
	return runner.Run(ctx, cmd, os.Stdout, os.Stderr)
}

func constructNinjaTargets(modules buildModules, staticSpec *fintpb.Static) []string {
	var targets []string

	if staticSpec.IncludeImages {
		targets = append(targets, extraTargetsForImages...)

		for _, image := range modules.Images() {
			if isTestingImage(image, staticSpec.Pave) {
				targets = append(targets, image.Path)
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
				if contains(archivesToBuild, archive.Name) {
					targets = append(targets, archive.Path)
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
		for _, zbiTest := range modules.ZBITests() {
			targets = append(targets, zbiTest.Path)
		}
	}

	if staticSpec.IncludePrebuiltBinaryManifests {
		for _, manifest := range modules.PrebuiltBinarySets() {
			targets = append(targets, manifest.Manifest)
		}
	}

	targets = append(targets, staticSpec.NinjaTargets...)

	sort.Strings(targets)
	return targets
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
	case image.Type == "scripts":
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
