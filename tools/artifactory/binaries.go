// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package artifactory

import (
	"fmt"
	"os"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
)

const (
	outputBreakpadSymsGNArg = "output_breakpad_syms"
)

// DebugBinaryUploads parses the binary manifest associated to a build and
// returns a list of Uploads of debug binaries and a list of associated fuchsia
// build IDs.
func DebugBinaryUploads(mods *build.Modules, namespace string) ([]Upload, []string, error) {
	return debugBinaryUploads(mods, namespace)
}

func debugBinaryUploads(mods binModules, namespace string) ([]Upload, []string, error) {
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
	buildIDSet := map[string]bool{}

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
		buildIDSet[id] = true

		// We upload all debug binaries to a flat namespace.
		debugSrc := filepath.Join(mods.BuildDir(), bin.Debug)
		uploads = append(uploads, Upload{
			Source:      debugSrc,
			Destination: fmt.Sprintf("%s/%s.debug", namespace, id),
			Deduplicate: true,
			Compress:    true,
		})

		// Ditto for breakpad symbols, if present.
		breakpadSrc := filepath.Join(mods.BuildDir(), bin.Breakpad)
		if bin.Breakpad != "" {
			uploads = append(uploads, Upload{
				Source:      breakpadSrc,
				Destination: fmt.Sprintf("%s/%s.sym", namespace, id),
				Deduplicate: true,
				Compress:    true,
			})
		}

		if bin.OS == "fuchsia" {
			fuchsiaBuildIDs = append(fuchsiaBuildIDs, id)
			// If we configured the build to output breakpad symbols, then
			// assert that they are present for every fuchsia binary that is
			// also present.
			//
			// There is generic already logic in cmd/up.go for checking whether
			// a file exists, so we leave the filtering to that.
			if breakpadEmitted {
				if bin.Breakpad == "" {
					return nil, nil, fmt.Errorf("breakpad symbol must be present for %s", bin.Label)
				}
				debugBuilt, err := osmisc.FileExists(debugSrc)
				if err != nil {
					return nil, nil, fmt.Errorf("failed to determine if debug binary %q was built: %v", bin.Label, err)
				}
				breakpadBuilt, err := osmisc.FileExists(breakpadSrc)
				if err != nil {
					return nil, nil, fmt.Errorf("failed to determine if breakpad file for %q was built: %v", bin.Label, err)
				}
				if debugBuilt && !breakpadBuilt {
					fmt.Printf("breakpad file for the following binary was not built:\n %#v\n", bin)
				}
			}
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
