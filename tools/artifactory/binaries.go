// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"fmt"
	"os"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
)

const (
	outputBreakpadSymsGNArg = "output_breakpad_syms"
)

// DebugBinaryUploads parses the binary manifest associated to a build and
// returns a list of Uploads of debug binaries and a list of associated fuchsia
// build IDs.
func DebugBinaryUploads(mods *build.Modules, debugNamespace, buildidNamespace string) ([]Upload, []string, error) {
	return debugBinaryUploads(mods, debugNamespace, buildidNamespace)
}

func debugBinaryUploads(mods binModules, debugNamespace, buildidNamespace string) ([]Upload, []string, error) {
	bins := mods.Binaries()
	for _, pb := range mods.PrebuiltBinaries() {
		if pb.Manifest == "" {
			continue
		}
		prebuiltBins, err := pb.Get(mods.BuildDir())
		// The manifest might not have been built, but that's okay.
		if os.IsNotExist(err) {
			continue
		} else if err != nil {
			return nil, nil, fmt.Errorf("failed to derive binaries from prebuilt binary set %q: %w", pb.Name, err)
		}
		bins = append(bins, prebuiltBins...)
	}

	var uploads []Upload
	var fuchsiaBuildIDs []string
	buildIDSet := make(map[string]struct{})

	breakpadEmitted, err := mods.Args().BoolValue(outputBreakpadSymsGNArg)
	if err != nil && err != build.ErrArgNotSet {
		return nil, nil, fmt.Errorf("failed to determine whether breakpad symbols were output in the build: %v", err)
	}

	for _, bin := range bins {
		id, err := bin.ELFBuildID(mods.BuildDir())
		// OK if there was no build ID found for an associated binary.
		if err == build.ErrBuildIDNotFound {
			continue
		} else if err != nil {
			return nil, nil, err
		}

		// Skip duplicate build IDs.
		if _, ok := buildIDSet[id]; ok {
			continue
		}
		buildIDSet[id] = struct{}{}

		// We upload all debug binaries to a flat namespace.
		debugSrc := filepath.Join(mods.BuildDir(), bin.Debug)
		debugBuilt, err := osmisc.FileExists(debugSrc)
		if err != nil {
			return nil, nil, fmt.Errorf("failed to determine if debug binary %q was built: %v", bin.Label, err)
		} else if !debugBuilt {
			return nil, nil, fmt.Errorf("something's wrong: we have a build ID for %q but no associated debug binary", bin.Label)
		}
		uploads = append(uploads, Upload{
			Source:      debugSrc,
			Destination: fmt.Sprintf("%s/%s.debug", debugNamespace, id),
			Deduplicate: true,
			Compress:    true,
		})

		// Upload in debuginfod API format.
		uploads = append(uploads, Upload{
			Source:      debugSrc,
			Destination: fmt.Sprintf("%s/%s/debuginfo", buildidNamespace, id),
			Deduplicate: true,
			Compress:    true,
		})
		if bin.Dist != "" {
			uploads = append(uploads, Upload{
				Source:      filepath.Join(mods.BuildDir(), bin.Dist),
				Destination: fmt.Sprintf("%s/%s/executable", buildidNamespace, id),
				Deduplicate: true,
				Compress:    true,
			})
		}

		// If we configured the build to output breakpad symbols, then
		// assert that the associated breakpad file here was present, as the
		// associated debug binary is.
		if breakpadEmitted {
			if bin.Breakpad == "" {
				if bin.OS == "fuchsia" {
					return nil, nil, fmt.Errorf("breakpad file for %q was not present in metadata", bin.Label)
				}
				// We're lenient on non-fuchsia binaries not having breakpad
				// files as we can't generate them for all hosts and languages.
				continue
			}
			breakpadSrc := filepath.Join(mods.BuildDir(), bin.Breakpad)
			breakpadBuilt, err := osmisc.FileExists(breakpadSrc)
			if err != nil {
				return nil, nil, fmt.Errorf("failed to determine if breakpad file for %q was built: %v", bin.Label, err)
			} else if !breakpadBuilt {
				return nil, nil, fmt.Errorf("breakpad file for %q was not built", bin.Label)
			}
			uploads = append(uploads, Upload{
				Source:      breakpadSrc,
				Destination: fmt.Sprintf("%s/%s.sym", debugNamespace, id),
				Deduplicate: true,
				Compress:    true,
			})
		}

		// At this point, fuchsiaBuildIDs should reflect all binaries built
		// and *all* of their associated breakpad files if emitted in the build.
		if bin.OS == "fuchsia" {
			fuchsiaBuildIDs = append(fuchsiaBuildIDs, id)
		}
	}
	return uploads, fuchsiaBuildIDs, nil
}

type binModules interface {
	BuildDir() string
	Args() build.Args
	Binaries() []build.Binary
	PrebuiltBinaries() []build.PrebuiltBinaries
}
