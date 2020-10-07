// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"
)

func TestParseStaticProto(t *testing.T) {
	textproto := `optimize: RELEASE
board: "qemu"
product: "workstation"
exclude_images: false
ninja_targets: "default"
include_host_tests: false
target_arch: X64
enforce_size_limits: false
collect_metrics: false
include_archives: false
skip_if_unaffected: true
`

	if _, err := parseStatic(textproto); err != nil {
		t.Errorf("failed to parse static .textproto: %s", err)
	}
}
