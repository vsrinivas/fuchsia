// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"go.fuchsia.dev/fuchsia/tools/build"
)

// AddImageDeps selects and adds the subset of images needed by a shard to
// that shard's list of dependencies.
func AddImageDeps(s *Shard, images []build.Image, pave bool) {
	imageDeps := []string{"images.json"}
	for _, image := range images {
		if isUsedForTesting(s, image, pave) {
			imageDeps = append(imageDeps, image.Path)
		}
	}
	s.AddDeps(imageDeps)
}

func isUsedForTesting(s *Shard, image build.Image, pave bool) bool {
	if s.Env.IsEmu {
		return image.Name == "qemu-kernel" || image.Name == "storage-full"
	}
	return (pave && len(image.PaveArgs) != 0) || (!pave && len(image.NetbootArgs) != 0)
}
