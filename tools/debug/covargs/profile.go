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
	Profile string   `json:"profile"`
	Modules []string `json:"modules"`
}

// ProfileEntries combines data from runtests and symbolizer, returning
// a sequence of entries, where each entry contains a raw profile and all
// modules (specified by build ID) present in that profile.
func MergeEntries(ctx context.Context, dumps map[string]symbolize.DumpEntry, summary runtests.DataSinkMap) ([]ProfileEntry, error) {
	entries := []ProfileEntry{}

	for _, sink := range summary[llvmProfileSinkType] {
		dump, ok := dumps[sink.Name]
		if !ok {
			logger.Warningf(ctx, "%s not found in summary file\n", sink.Name)
			continue
		}

		modules := []string{}
		moduleSet := make(map[string]struct{})
		for _, mod := range dump.Modules {
			if _, ok := moduleSet[mod.Build]; !ok {
				modules = append(modules, mod.Build)
				moduleSet[mod.Build] = struct{}{}
			}
		}

		entries = append(entries, ProfileEntry{
			Modules: modules,
			Profile: sink.File,
		})
	}

	return entries, nil
}
