// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package buildbucket

import (
	buildbucketpb "go.chromium.org/luci/buildbucket/proto"
)

// Build is a helper for reading Buildbucket Build information.
type Build buildbucketpb.Build

// GitilesCommit returns the Gitiles commit that triggered this build, or nil if this
// build was not triggered by a Gitiltes commit.
func (b Build) GitilesCommit() *buildbucketpb.GitilesCommit {
	return b.Input.GitilesCommit
}

// Property reads a specific input Property from this builder. Returns false with a nil
// Property if not found.
func (b Build) Property(name string) (*Property, bool) {
	prop, ok := b.Input.Properties.Fields[name]
	if !ok {
		return nil, false
	}
	return &Property{name: name, value: prop}, true
}
