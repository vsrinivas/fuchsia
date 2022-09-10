// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"fmt"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// ProductSizeCheckerOutputUploads generates the Uploads for the output of product size checker.
func ProductSizeCheckerOutputUploads(mods *build.Modules, namespace string) ([]Upload, error) {
	return productSizeCheckerOutputUploads(mods, namespace)
}

func productSizeCheckerOutputUploads(mods productSizeCheckerOutputModules, namespace string) ([]Upload, error) {
	// There should be either 0 or 1 ProductSizeCheckerOutputs.
	if len(mods.ProductSizeCheckerOutput()) == 0 {
		return []Upload{}, nil
	} else if len(mods.ProductSizeCheckerOutput()) == 1 {
		return []Upload{
			{
				Source:      filepath.Join(mods.BuildDir(), mods.ProductSizeCheckerOutput()[0].Visualization),
				Destination: path.Join(namespace, "visualization"),
				Compress:    true,
			},
			{
				Source:      filepath.Join(mods.BuildDir(), mods.ProductSizeCheckerOutput()[0].SizeBreakdown),
				Destination: path.Join(namespace, "size_breakdown.txt"),
			},
		}, nil
	} else {
		return nil, fmt.Errorf("Expected 0 or 1 ProductSizeCheckerOutputs, found %d", len(mods.ProductSizeCheckerOutput()))
	}
}

type productSizeCheckerOutputModules interface {
	BuildDir() string
	ProductSizeCheckerOutput() []build.ProductSizeCheckerOutput
}
