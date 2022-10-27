// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package updater

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"io"
	"net/url"
	"os"
	"path/filepath"
	"time"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/avb"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/omaha_tool"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/packages"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/zbi"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"

	"golang.org/x/crypto/ssh"
)

const (
	updateErrorSleepTime = 30 * time.Second
)

type client interface {
	ExpectReboot(ctx context.Context, f func() error) error
	DisconnectionListener() <-chan struct{}
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
	return updateCheckNow(ctx, c, u.repo, true)
}

func updateCheckNow(ctx context.Context, c client, repo *packages.Repository, createRewriteRule bool) error {
	logger.Infof(ctx, "Triggering OTA")

	startTime := time.Now()
	err := c.ExpectReboot(ctx, func() error {
		// Since an update can trigger a reboot, we can run into all
		// sorts of races. The two main ones are:
		//
		//  * the network connection is torn down before we see the
		//    `update` command exited cleanly.
		//  * the system updater service was torn down before the
		//    `update` process, which would show up as the channel to
		//    be closed.
		//
		// In order to avoid this races, we need to:
		//
		//  * assume the ssh connection was closed means the OTA was
		//    probably installed and the device rebooted as normal.
		//  * `update` exiting with a error could be we just lost the
		//    shutdown race. So if we get an `update` error, wait a few
		//    seconds to see if the device disconnects. If so, treat it
		//    like the OTA was successful.

		// We pass createRewriteRule=true for versions of system-update-checker prior to
		// fxrev.dev/504000. Newer versions need to have `update channel set` called below.
		server, err := c.ServePackageRepository(ctx, repo, "trigger-ota", createRewriteRule)
		if err != nil {
			return fmt.Errorf("error setting up server: %w", err)
		}
		defer server.Shutdown(ctx)

		ch := c.DisconnectionListener()

		cmd := []string{
			"/bin/update",
			"channel",
			"set",
			"trigger-ota",
		}
		if err := c.Run(ctx, cmd, os.Stdout, os.Stderr); err != nil {
			logger.Warningf(ctx, "update channel set failed: %v. This probably indicates the device is running an old version of system-update-checker.", err)
		}

		cmd = []string{
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
			if errors.As(err, &errExitMissing) {
				logger.Warningf(ctx, "disconnected, assuming this was because OTA triggered reboot")
				return nil
			}

			logger.Warningf(ctx, "update errored out, but maybe it lost the race, waiting a moment to see if the device reboots: %v", err)

			// We got an error, but maybe we lost the reboot race.
			// So wait a few moments to see if the device reboots
			// anyway.
			select {
			case <-ch:
				logger.Warningf(ctx, "disconnected, assuming this was because OTA triggered reboot")
				return nil
			case <-time.After(updateErrorSleepTime):
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
	omahaTool        *omaha_tool.OmahaTool
	avbTool          *avb.AVBTool
	zbiTool          *zbi.ZBITool
}

func NewOmahaUpdater(
	repo *packages.Repository,
	updatePackageURL string,
	omahaTool *omaha_tool.OmahaTool,
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
		omahaTool:        omahaTool,
		avbTool:          avbTool,
		zbiTool:          zbiTool,
	}, nil
}

func (u *OmahaUpdater) Update(ctx context.Context, c client) error {
	logger.Infof(ctx, "injecting omaha_url into %q", u.updatePackageURL)

	pkg, err := u.repo.OpenPackage(ctx, u.updatePackageURL.Path[1:])
	if err != nil {
		return fmt.Errorf("failed to open url %q: %w", u.updatePackageURL, err)
	}

	logger.Infof(ctx, "source update package merkle for %q is %q", u.updatePackageURL, pkg.Merkle())

	tempDir, err := os.MkdirTemp("", "update-pkg-expand")
	if err != nil {
		return fmt.Errorf("unable to create temp directory: %w", err)
	}
	defer os.RemoveAll(tempDir)

	if err := pkg.Expand(ctx, tempDir); err != nil {
		return fmt.Errorf("failed to expand pkg to %s: %w", tempDir, err)
	}

	// Create a ZBI with the omaha_url argument.
	destZbi, err := os.CreateTemp("", "omaha_argument.zbi")
	if err != nil {
		return fmt.Errorf("failed to create temp file: %w", err)
	}
	defer os.Remove(destZbi.Name())

	imageArguments := map[string]string{
		"omaha_url": u.omahaTool.URL(),
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

	logger.Infof(ctx, "Omaha Server URL set in vbmeta to %q", u.omahaTool.URL())

	// Update packages.json in this package.
	packagesJsonPath := filepath.Join(tempDir, "packages.json")
	err = util.AtomicallyWriteFile(packagesJsonPath, 0600, func(f *os.File) error {
		src, err := os.Open(packagesJsonPath)
		if err != nil {
			return fmt.Errorf("Failed to open packages.json %q: %w", packagesJsonPath, err)
		}
		if err := util.RehostPackagesJSON(bufio.NewReader(src), bufio.NewWriter(f), "trigger-ota"); err != nil {
			return fmt.Errorf("Failed to rehost packages.json: %w", err)
		}

		return nil
	})
	if err != nil {
		return fmt.Errorf("Failed to atomically overwrite %q: %w", packagesJsonPath, err)
	}

	logger.Infof(ctx, "host names in packages.json set to trigger-ota")

	pkgBuilder, err := packages.NewPackageBuilderFromDir(tempDir, "update_omaha", "0", "testrepository.com")
	if err != nil {
		return fmt.Errorf("Failed to parse package from %q: %w", tempDir, err)
	}
	defer pkgBuilder.Close()

	pkgPath, pkgMerkle, err := pkgBuilder.Publish(ctx, u.repo)
	if err != nil {
		return fmt.Errorf("Failed to publish update package: %w", err)
	}

	logger.Infof(ctx, "published %q as %q to %q", pkgPath, pkgMerkle, u.repo)

	omahaPackageURL := fmt.Sprintf("fuchsia-pkg://trigger-ota/%s?hash=%s", pkgPath, pkgMerkle)

	// Configure the Omaha server with the new omaha package URL.
	if err := u.omahaTool.SetPkgURL(ctx, omahaPackageURL); err != nil {
		return fmt.Errorf("Failed to set Omaha update package: %w", err)
	}

	// Trigger an update
	return updateCheckNow(ctx, c, u.repo, false)
}
