// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"fmt"
	"strings"

	"fuchsia.googlesource.com/host_target_testing/util"
)

// Builder allows fetching build information from a specific builder.
type Builder struct {
	archive *Archive
	name    string
}

// LookupBuildID looks up the latest build id for a given builder.
func (b *Builder) GetLatestBuildID() (string, error) {
	stdout, stderr, err := util.RunCommand(b.archive.lkgbPath, b.name)
	if err != nil {
		return "", fmt.Errorf("lkgb failed: %s: %s", err, string(stderr))
	}
	return strings.TrimRight(string(stdout), "\n"), nil
}

// GetLatestBuild looks up the latest build for a given builder.
func (b *Builder) GetLatestBuild(dir string) (*Build, error) {
	id, err := b.GetLatestBuildID()
	if err != nil {
		return nil, err
	}

	return b.archive.GetBuildByID(id, dir)
}

func (b *Builder) String() string {
	return b.name
}
