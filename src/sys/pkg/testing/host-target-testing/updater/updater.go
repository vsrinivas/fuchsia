// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package updater

import (
	"context"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"net/url"
	"os"
	"path/filepath"
	"time"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/avb"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/omaha"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/packages"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/zbi"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"

	"golang.org/x/crypto/ssh"
)

type client interface {
	ExpectReboot(ctx context.Context, f func() error) error
	ServePackageRepository(
		ctx context.Context,
		repo *packages.Repository,
		name string,
		createRewriteRule bool) (*packages.Server, error)
	Run(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) error
}

type Updater interface {
	Update(ctx context.Context, c client) error
}

// SystemUpdateChecker uses `update check-now` to install a package.
type SystemUpdateChecker struct {
	repo *packages.Repository
}

func NewSystemUpdateChecker(repo *packages.Repository) *SystemUpdateChecker {
	return &SystemUpdateChecker{repo: repo}
}

func (u *SystemUpdateChecker) Update(ctx context.Context, c client) error {
	logger.Infof(ctx, "Triggering OTA")

	startTime := time.Now()
	err := c.ExpectReboot(ctx, func() error {
		server, err := c.ServePackageRepository(ctx, u.repo, "trigger-ota", true)
		if err != nil {
			return fmt.Errorf("error setting up server: %w", err)
		}
		defer server.Shutdown(ctx)
		cmd := []string{
			"/bin/update",
			"check-now",
			"--monitor",
		}
		if err := c.Run(ctx, cmd, os.Stdout, os.Stderr); err != nil {
			// If the device rebooted before ssh was able to tell
			// us the command ran, it will tell us the session
			// exited without passing along an exit code. So,
			// ignore that specific error.
			var errExitMissing *ssh.ExitMissingError
			if !errors.As(err, &errExitMissing) {
				return fmt.Errorf("failed to trigger OTA: %w", err)
			}
		}
		return nil
	})
	if err != nil {
		return err
	}

	logger.Infof(ctx, "OTA completed in %s", time.Now().Sub(startTime))

	return nil
}

// SystemUpdater uses the `system-updater` to install a package.
type SystemUpdater struct {
	repo             *packages.Repository
	updatePackageUrl string
}

func NewSystemUpdater(repo *packages.Repository, updatePackageUrl string) *SystemUpdater {
	return &SystemUpdater{repo: repo, updatePackageUrl: updatePackageUrl}
}

func (u *SystemUpdater) Update(ctx context.Context, c client) error {
	startTime := time.Now()
	server, err := c.ServePackageRepository(ctx, u.repo, "download-ota", true)
	if err != nil {
		return fmt.Errorf("error setting up server: %w", err)
	}
	defer server.Shutdown(ctx)

	logger.Infof(ctx, "Downloading OTA %q", u.updatePackageUrl)

	cmd := []string{
		"update",
		"force-install",
		"--reboot", "false",
		fmt.Sprintf("%q", u.updatePackageUrl),
	}
	if err := c.Run(ctx, cmd, os.Stdout, os.Stderr); err != nil {
		return fmt.Errorf("failed to run system updater: %w", err)
	}

	logger.Infof(ctx, "OTA successfully downloaded in %s", time.Now().Sub(startTime))

	return nil
}

type OmahaUpdater struct {
	repo             *packages.Repository
	updatePackageURL *url.URL
	omahaServer      *omaha.OmahaServer
	avbTool          *avb.AVBTool
	zbiTool          *zbi.ZBITool
}

func NewOmahaUpdater(
	repo *packages.Repository,
	updatePackageURL string,
	omahaServer *omaha.OmahaServer,
	avbTool *avb.AVBTool,
	zbiTool *zbi.ZBITool,
) (*OmahaUpdater, error) {
	u, err := url.Parse(updatePackageURL)
	if err != nil {
		return nil, fmt.Errorf("invalid update package URL %q: %w", updatePackageURL, err)
	}

	if u.Scheme != "fuchsia-pkg" {
		return nil, fmt.Errorf("scheme must be 'fuchsia-pkg', not %q", u.Scheme)
	}

	if u.Host == "" {
		return nil, fmt.Errorf("update package URL's host must not be empty")
	}

	return &OmahaUpdater{
		repo:             repo,
		updatePackageURL: u,
		omahaServer:      omahaServer,
		avbTool:          avbTool,
		zbiTool:          zbiTool,
	}, nil
}

func (u *OmahaUpdater) Update(ctx context.Context, c client) error {
	logger.Infof(ctx, "injecting omaha_url into %q", u.updatePackageURL)

	pkg, err := u.repo.OpenPackage(u.updatePackageURL.Path[1:])
	if err != nil {
		return fmt.Errorf("failed to open url %q: %w", u.updatePackageURL, err)
	}

	logger.Infof(ctx, "source update package merkle for %q is %q", u.updatePackageURL, pkg.Merkle())

	tempDir, err := ioutil.TempDir("", "update-pkg-expand")
	if err != nil {
		return fmt.Errorf("unable to create temp directory: %w", err)
	}
	defer os.RemoveAll(tempDir)

	if err := pkg.Expand(tempDir); err != nil {
		return fmt.Errorf("failed to expand pkg to %s: %w", tempDir, err)
	}

	// Create a ZBI with the omaha_url argument.
	destZbi, err := ioutil.TempFile("", "omaha_argument.zbi")
	if err != nil {
		return fmt.Errorf("failed to create temp file: %w", err)
	}
	defer os.Remove(destZbi.Name())

	imageArguments := map[string]string{
		"omaha_url": u.omahaServer.URL(),
	}

	if err := u.zbiTool.MakeImageArgsZbi(ctx, destZbi.Name(), imageArguments); err != nil {
		return fmt.Errorf("failed to create ZBI: %w", err)
	}

	// Create a vbmeta that includes the ZBI we just created.
	propFiles := map[string]string{
		"zbi": destZbi.Name(),
	}

	// Update vbmeta in this package.
	srcVbmetaPath := filepath.Join(tempDir, "fuchsia.vbmeta")

	if _, err := os.Stat(srcVbmetaPath); err != nil {
		return fmt.Errorf("vbmeta %q does not exist in repo: %w", srcVbmetaPath, err)
	}

	// Swap the the updated vbmeta into place.
	err = util.AtomicallyWriteFile(srcVbmetaPath, 0600, func(f *os.File) error {
		if err := u.avbTool.MakeVBMetaImage(ctx, f.Name(), srcVbmetaPath, propFiles); err != nil {
			return fmt.Errorf("Failed to update vbmeta: %w", err)
		}

		return nil
	})
	if err != nil {
		return fmt.Errorf("failed to atomically overwrite %q: %w", srcVbmetaPath, err)
	}

	logger.Infof(ctx, "Omaha Server URL set in vbmeta to %q", u.omahaServer.URL())

	pkgBuilder, err := packages.NewPackageBuilderFromDir(tempDir, "update_omaha", "0")
	if err != nil {
		return fmt.Errorf("Failed to parse package from %q: %w", tempDir, err)
	}
	defer pkgBuilder.Close()

	pkgPath, pkgMerkle, err := pkgBuilder.Publish(ctx, u.repo)
	if err != nil {
		return fmt.Errorf("Failed to publish update package: %w", err)
	}

	logger.Infof(ctx, "published %q as %q to %q", pkgPath, pkgMerkle, u.repo)

	omahaPackageURL := fmt.Sprintf("fuchsia-pkg://fuchsia.com/%s?hash=%s", pkgPath, pkgMerkle)

	// Have the omaha server serve the package.
	if err := u.omahaServer.SetUpdatePkgURL(ctx, omahaPackageURL); err != nil {
		return fmt.Errorf("Failed to set Omaha update package: %w", err)
	}

	// Trigger an update
	updateChecker := NewSystemUpdateChecker(u.repo)

	return updateChecker.Update(ctx, c)
}
