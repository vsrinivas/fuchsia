// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package updater

import (
	"context"
	"fmt"
	"io"
	"log"
	"os"
	"time"

	"fuchsia.googlesource.com/host_target_testing/packages"

	"golang.org/x/crypto/ssh"
)

type client interface {
	ReadBasePackages(ctx context.Context) (map[string]string, error)
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
	log.Printf("Triggering OTA")
	startTime := time.Now()
	basePackages, err := c.ReadBasePackages(ctx)
	if err != nil {
		return err
	}
	updateBinMerkle, ok := basePackages["update-bin/0"]
	if !ok {
		return fmt.Errorf("base packages doesn't include update-bin/0 package")
	}
	return c.ExpectReboot(ctx, func() error {
		server, err := c.ServePackageRepository(ctx, u.repo, "trigger-ota", true)
		if err != nil {
			return fmt.Errorf("error setting up server: %s", err)
		}
		defer server.Shutdown(ctx)
		// FIXME: running this out of /pkgfs/versions is unsound WRT using the correct loader service
		// Adding this as a short-term hack to unblock http://fxb/47213
		cmd := []string{
			fmt.Sprintf("/pkgfs/versions/%s/bin/update", updateBinMerkle),
			"check-now",
			"--monitor",
		}
		if err := c.Run(ctx, cmd, os.Stdout, os.Stderr); err != nil {
			// If the device rebooted before ssh was able to tell
			// us the command ran, it will tell us the session
			// exited without passing along an exit code. So,
			// ignore that specific error.
			if _, ok := err.(*ssh.ExitMissingError); !ok {
				return fmt.Errorf("failed to trigger OTA: %s", err)
			}
		}
		return nil
	})
	if err != nil {
		return err
	}
	log.Printf("OTA completed in %s", time.Now().Sub(startTime))
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
	log.Printf("Downloading OTA")
	startTime := time.Now()
	server, err := c.ServePackageRepository(ctx, u.repo, "download-ota", true)
	if err != nil {
		return fmt.Errorf("error setting up server: %s", err)
	}
	defer server.Shutdown(ctx)
	// In order to manually trigger the system updater, we need the `run`
	// package. Since builds can be configured to not automatically install
	// packages, we need to explicitly resolve it.
	cmd := []string{"pkgctl", "resolve", "fuchsia-pkg://fuchsia.com/run/0"}
	if err := c.Run(ctx, cmd, os.Stdout, os.Stderr); err != nil {
		return fmt.Errorf("error resolving the run package: %v", err)
	}
	log.Printf("Downloading system OTA")
	cmd = []string{
		"run",
		"\"fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx\"",
		"--initiator", "manual",
		// Go's boolean flag parsing requires that the argument name and value
		// be separated by "=" instead of by whitespace.
		"--reboot=false",
		"--update", fmt.Sprintf("%q", u.updatePackageUrl),
	}
	if err := c.Run(ctx, cmd, os.Stdout, os.Stderr); err != nil {
		return fmt.Errorf("failed to run system_updater.cmx: %s", err)
	}
	log.Printf("OTA successfully downloaded in %s", time.Now().Sub(startTime))
	return nil
}
