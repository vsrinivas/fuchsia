// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ffxutil

import (
	"context"
	"fmt"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	flashManifestPath = "flash.json"
)

// flashManifest represents a manifest file used by `ffx target flash`.
type flashManifest struct {
	Manifest manifest `json:"manifest"`
}

type manifest struct {
	Credentials []string  `json:"credentials"`
	Products    []product `json:"products"`
}

type product struct {
	BootloaderPartitions []partition `json:"bootloader_partitions"`
	Name                 string      `json:"name"`
	Partitions           []partition `json:"partitions"`
}

type partition struct {
	Path string `json:"path"`
}

// GetFlashDeps returns the list of file dependencies for `ffx target flash`.
func GetFlashDeps(sdkRoot, productName string) ([]string, error) {
	manifestPath := filepath.Join(sdkRoot, flashManifestPath)
	var manifest flashManifest
	if err := jsonutil.ReadFromFile(manifestPath, &manifest); err != nil {
		return []string{}, fmt.Errorf("failed to read flash manifest: %w", err)
	}
	deps := map[string]struct{}{
		flashManifestPath: {},
	}
	for _, cred := range manifest.Manifest.Credentials {
		deps[cred] = struct{}{}
	}
	foundProduct := false
	for _, product := range manifest.Manifest.Products {
		if product.Name != productName {
			continue
		}
		foundProduct = true
		for _, partition := range append(product.BootloaderPartitions, product.Partitions...) {
			deps[partition.Path] = struct{}{}
		}
	}
	if !foundProduct {
		return []string{}, fmt.Errorf("failed to find product %s in manifest %s", productName, manifestPath)
	}
	var depsList []string
	for dep := range deps {
		depsList = append(depsList, dep)
	}
	return depsList, nil
}

// Flash flashes the target.
func (f *FFXInstance) Flash(ctx context.Context, serialNum, manifest, sshKey string) error {
	if err := f.ConfigSet(ctx, "ffx.fastboot.inline_target", "true"); err != nil {
		return err
	}
	// Setting the `ffx.fastboot.inline_target` field causes ffx to assume the target
	// it's trying to reach is in fastboot mode. We need to reset it to false after
	// we're done flashing.
	defer func() {
		if err := f.ConfigSet(ctx, "ffx.fastboot.inline_target", "false"); err != nil {
			logger.Errorf(ctx, "failed to reset ffx.fastboot.inline_target to false")
		}
	}()
	return f.Run(ctx, "--target", serialNum, "target", "flash", "--authorized-keys", sshKey, manifest)
}
