// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package hostplatform

import (
	"fmt"
	"runtime"
	"strings"
)

// Name returns the name of the current platform, in the form "<os>-<cpu_arch>".
// E.g. "linux-arm64" or "mac-x64". This is useful for finding the path within
// the checkout to a platform-specific prebuilt.
func Name() (string, error) {
	return name(runtime.GOOS, runtime.GOARCH)
}

// MakeName constructs the canonical name for a platform given its OS and CPU
// architecture.
func MakeName(os, arch string) string {
	return fmt.Sprintf("%s-%s", os, arch)
}

// name is extracted for testability.
func name(goOS, goArch string) (string, error) {
	os, ok := map[string]string{
		"windows": "win",
		"darwin":  "mac",
		"linux":   "linux",
	}[goOS]
	if !ok {
		return "", fmt.Errorf("unsupported GOOS %q", goOS)
	}

	arch, ok := map[string]string{
		"amd64": "x64",
		"arm64": "arm64",
	}[goArch]
	if !ok {
		return "", fmt.Errorf("unsupported GOARCH %q", goArch)
	}
	return MakeName(os, arch), nil
}

// IsMac determines whether the platform corresponds to a Mac platform.
func IsMac(platform string) bool {
	return strings.HasPrefix(platform, "mac-")
}
