// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"
	"io"
	"path/filepath"
)

type subprocessRunner interface {
	Run(ctx context.Context, cmd []string, stdout, stderr io.Writer) error
	RunWithStdin(ctx context.Context, cmd []string, stdout, stderr io.Writer, stdin io.Reader) error
}

// thirdPartyPrebuilt returns the absolute path to a platform-specific prebuilt
// in the //prebuilt/third_party subdirectory of the checkout.
func thirdPartyPrebuilt(checkoutDir, platform, name string) string {
	return filepath.Join(checkoutDir, "prebuilt", "third_party", name, platform, name)
}

func contains(items []string, target string) bool {
	for _, item := range items {
		if item == target {
			return true
		}
	}
	return false
}
