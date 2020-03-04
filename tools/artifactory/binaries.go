// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package artifactory

import (
	"fmt"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

// DebugBinaryUploads parses the binary manifest associated to a build and
// returns a list of Uploads of debug binaries and a list of associated fuchsia
// build IDs.
func DebugBinaryUploads(mods *build.Modules, namespace string) ([]Upload, []string, error) {
	return debugBinaryUploads(mods, namespace)
}

func debugBinaryUploads(mods binModules, namespace string) ([]Upload, []string, error) {
	bins := mods.Binaries()
	for _, pkg := range mods.PrebuiltPackages() {
		if pkg.BinaryManifest == "" {
			continue
		}
		prebuiltBins, err := pkg.Binaries(mods.BuildDir())
		if err != nil {
			return nil, nil, fmt.Errorf("failed to derive binaries from prebuilt package %q: %v", pkg.Name, err)
		}
		bins = append(bins, prebuiltBins...)
	}

	var uploads []Upload
	var fuchsiaBuildIDs []string
	buildIDSet := map[string]bool{}

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

		if bin.OS == "fuchsia" {
			fuchsiaBuildIDs = append(fuchsiaBuildIDs, id)
		}

		// We upload all debug binaries to a flat namespace.
		debugDest := fmt.Sprintf("%s/%s.debug", namespace, id)
		uploads = append(uploads, Upload{
			Source:      filepath.Join(mods.BuildDir(), bin.Debug),
			Destination: debugDest,
			Deduplicate: true,
		})

		// Ditto for breakpad symbols, if present.
		if bin.Breakpad != "" {
			breakpadDest := fmt.Sprintf("%s/%s.sym", namespace, id)
			uploads = append(uploads, Upload{
				Source:      filepath.Join(mods.BuildDir(), bin.Breakpad),
				Destination: breakpadDest,
				Deduplicate: true,
			})
		}
	}

	return uploads, fuchsiaBuildIDs, nil
}

type binModules interface {
	BuildDir() string
	Binaries() []build.Binary
	PrebuiltPackages() []build.PrebuiltPackage
}
