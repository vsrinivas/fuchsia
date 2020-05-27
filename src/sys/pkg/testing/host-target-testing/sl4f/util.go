// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sl4f

import (
	"context"
	"fmt"
	"os"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

// ValidateStaticPackages checks that all static packages have no missing blobs.
func (c *Client) ValidateStaticPackages(ctx context.Context) error {
	logger.Infof(ctx, "validating static packages")

	path := "/pkgfs/ctl/validation/missing"
	f, err := c.FileRead(ctx, path)
	if err != nil {
		return fmt.Errorf("error reading %q: %w", path, err)
	}

	merkles := strings.TrimSpace(string(f))
	if merkles != "" {
		return fmt.Errorf("static packages are missing the following blobs:\n%s", merkles)
	}

	logger.Infof(ctx, "all static package blobs are accounted for")
	return nil
}

// GetSystemImageMerkle returns the merkle root of the system_image package
func (c *Client) GetSystemImageMerkle(ctx context.Context) (string, error) {
	path := "/system/meta"
	f, err := c.FileRead(ctx, path)
	if err != nil {
		return "", fmt.Errorf("error reading %q: %w", path, err)
	}

	return strings.TrimSpace(string(f)), nil
}

// PathExists determines if the path exists on the target.
func (c *Client) PathExists(ctx context.Context, path string) (bool, error) {
	_, err := c.PathStat(ctx, path)
	if err == nil {
		return true, nil
	} else if os.IsNotExist(err) {
		return false, nil
	}
	return false, err
}
