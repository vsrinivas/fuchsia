// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package license

type LicenseConfig struct {
	PatternRoots []*PatternRoot `json:"patternRoot"`
}

type PatternRoot struct {
	Paths []string `json:"paths"`
	Notes []string `json:"notes"`
}

var Config *LicenseConfig

func (c *LicenseConfig) Merge(other *LicenseConfig) {
	if c.PatternRoots == nil {
		c.PatternRoots = make([]*PatternRoot, 0)
	}
	if other.PatternRoots == nil {
		other.PatternRoots = make([]*PatternRoot, 0)
	}
	c.PatternRoots = append(c.PatternRoots, other.PatternRoots...)
}
