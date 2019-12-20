// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package covargs

import (
	"context"

	"go.fuchsia.dev/fuchsia/tools/debug/symbolize/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

const llvmProfileSinkType = "llvm-profile"

type ProfileEntry struct {
	ProfileData string   `json:"profile"`
	ModuleFiles []string `json:"modules"`
}

func MergeProfiles(ctx context.Context, dumps map[string]symbolize.DumpEntry, summary runtests.DataSinkMap, repo symbolize.Repository) ([]ProfileEntry, error) {
	entries := []ProfileEntry{}

	for _, sink := range summary[llvmProfileSinkType] {
		dump, ok := dumps[sink.Name]
		if !ok {
			logger.Warningf(ctx, "%s not found in summary file\n", sink.Name)
			continue
		}

		// This is going to go in a covDataEntry as the list of paths to the modules for the data
		moduleFiles := []string{}
		for _, mod := range dump.Modules {
			file, err := repo.GetBuildObject(mod.Build)
			if err != nil {
				logger.Warningf(ctx, "module with build id %s not found\n", mod.Build)
				continue
			}
			moduleFiles = append(moduleFiles, file.String())
			file.Close()
		}

		// Finally we can add all the data
		entries = append(entries, ProfileEntry{
			ModuleFiles: moduleFiles,
			ProfileData: sink.File,
		})
	}

	return entries, nil
}
