// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

var (
	AllTargets map[string]*Target
)

func init() {
	AllTargets = make(map[string]*Target)
}
