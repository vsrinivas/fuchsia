// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"
	"fmt"
	"net/url"
	"path/filepath"
	"strings"

	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
)

type subprocessRunner interface {
	Run(ctx context.Context, cmd []string, options subprocess.RunOptions) error
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

// makeAbsolute takes a root directory and a list of relative paths of files
// within that directory, and returns a list of absolute paths to those files.
func makeAbsolute(rootDir string, paths []string) []string {
	var res []string
	for _, path := range paths {
		res = append(res, filepath.Join(rootDir, path))
	}
	return res
}

// saveLogs writes the given set of logs to files in the artifact directory,
// and adds each path to the output artifacts.
func saveLogs(artifactDir string, artifacts *fintpb.BuildArtifacts, logs map[string]string) error {
	if artifactDir == "" {
		return nil
	}
	if artifacts.LogFiles == nil {
		artifacts.LogFiles = make(map[string]string)
	}
	for name, contents := range logs {
		dest := filepath.Join(
			artifactDir,
			url.QueryEscape(strings.ReplaceAll(name, " ", "_")))
		f, err := osmisc.CreateFile(dest)
		if err != nil {
			return err
		}
		defer f.Close()
		if _, err := f.WriteString(contents); err != nil {
			return fmt.Errorf("failed to write log file %q: %w", name, err)
		}
		artifacts.LogFiles[name] = f.Name()
	}
	return nil
}
