// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package testrunner

import (
	"context"
	"fmt"
	"net"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/integration/testsharder"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
)

// ResolveTestPackages resolves all test packages serially used by the given slice of tests.
func ResolveTestPackages(ctx context.Context, tests []testsharder.Test, addr net.IPAddr, sshKeyFile, resolveLog string) error {
	client, err := sshToTarget(ctx, addr, sshKeyFile)
	if err != nil {
		return fmt.Errorf("failed to establish an SSH connection: %w", err)
	}
	defer client.Close()

	l, err := osmisc.CreateFile(resolveLog)
	if err != nil {
		return fmt.Errorf("failed to create log for package resolutions: %w", err)
	}
	defer l.Close()

	urls := make(map[string]struct{})
	var orderedURLs []string
	for _, test := range tests {
		// Removes the resource path from the package URL, as pkgctl does not
		// support resource paths.
		pkgURL := strings.Split(test.PackageURL, "#")[0]
		if _, exists := urls[pkgURL]; !exists {
			urls[pkgURL] = struct{}{}
			orderedURLs = append(orderedURLs, pkgURL)
		}
	}

	// Resolve each of the packages serially.
	// Note that this package resolution is best effort, as run-test-suite
	// invocations will force a resolution even if this fails.
	for _, url := range orderedURLs {
		select {
		case <-ctx.Done():
			return nil
		case <-time.After(2 * time.Second):
			cmd := []string{"pkgctl", "resolve", url}
			backoff := retry.NewConstantBackoff(1 * time.Second)
			const maxReconnectAttempts = 3
			retry.Retry(ctx, retry.WithMaxAttempts(backoff, maxReconnectAttempts), func() error {
				if err := client.Run(ctx, cmd, l, l); err != nil {
					if !sshutil.IsConnectionError(err) {
						return retry.Fatal(err)
					}
					if err := client.Reconnect(ctx); err != nil {
						return retry.Fatal(err)
					}
					return err
				}
				return nil
			}, nil)
		}
	}
	return nil
}
