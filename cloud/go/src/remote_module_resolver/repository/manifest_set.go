// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package repository

// set of manifests ID'd by their moduleUrl.
type ManifestSet map[ModuleUrl]struct{}

// This modifies |dest| by leaving only the keys that are both in |dest| *and*
// |source|. |source| is left unchanged.
func (dest ManifestSet) intersect(source ManifestSet) {
	for moduleUrl := range dest {
		if _, ok := source[moduleUrl]; !ok {
			delete(dest, moduleUrl)
		}
	}
}

// This modifies |dest| by including all things in |source|.
func (dest ManifestSet) merge(source ManifestSet) {
	for moduleUrl := range source {
		dest[moduleUrl] = struct{}{}
	}
}

func listToManifestSet(l []ModuleUrl) ManifestSet {
	set := make(ManifestSet)
	for _, item := range l {
		set[item] = struct{}{}
	}
	return set
}
