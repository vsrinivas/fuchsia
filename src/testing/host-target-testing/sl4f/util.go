// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sl4f

import (
	"fmt"
	"log"
	"os"
	"strings"
)

// ValidateStaticPackages checks that all static packages have no missing blobs.
func (c *Client) ValidateStaticPackages() error {
	log.Printf("validating static packages")

	path := "/pkgfs/ctl/validation/missing"
	f, err := c.FileRead(path)
	if err != nil {
		return fmt.Errorf("error reading %q: %s", path, err)
	}

	merkles := strings.TrimSpace(string(f))
	if merkles != "" {
		return fmt.Errorf("static packages are missing the following blobs:\n%s", merkles)
	}

	log.Printf("all static package blobs are accounted for")
	return nil
}

// GetSystemImageMerkle returns the merkle root of the system_image package
func (c *Client) GetSystemImageMerkle() (string, error) {
	path := "/system/meta"
	f, err := c.FileRead(path)
	if err != nil {
		return "", fmt.Errorf("error reading %q: %s", path, err)
	}

	return strings.TrimSpace(string(f)), nil
}

// PathExists determines if the path exists on the target.
func (c *Client) PathExists(path string) (bool, error) {
	_, err := c.PathStat(path)
	if err == nil {
		return true, nil
	} else if os.IsNotExist(err) {
		return false, nil
	}
	return false, err
}
