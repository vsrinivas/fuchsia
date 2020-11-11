// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !fuchsia

package repo

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httputil"
	"os"
	"time"

	tuf_data "github.com/flynn/go-tuf/data"
	"github.com/pkg/sftp"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
)

const (
	defaultInstallPath = "/tmp/config.json"
	rootJSON           = "root.json"

	// Exponentially backoff in retrying fetching root.json at times 0.5s, 1s, 2s, 4s, 8s.
	backoffFloor      = 500 * time.Millisecond
	backoffCeiling    = 8 * time.Second
	backoffMultiplier = 2.0
)

// AddFromConfig writes the given config to the given remote install path and
// adds it as an update source.
func AddFromConfig(ctx context.Context, client *sshutil.Client, config *Config) error {
	sh, err := newRemoteShell(client)
	if err != nil {
		return err
	}
	defer func() {
		if err := sh.sftpClient.Remove(defaultInstallPath); err != nil {
			logger.Errorf(ctx, "failed to remove %q: %v", defaultInstallPath, err)
		}
		if err := sh.sftpClient.Close(); err != nil {
			logger.Errorf(ctx, "failed to close SFTP client: %v", err)
		}
	}()
	return addFromConfig(ctx, config, sh, defaultInstallPath)
}

func addFromConfig(ctx context.Context, config *Config, sh shell, installPath string) error {
	for i := range config.Mirrors {
		mirror := &config.Mirrors[i]
		if mirror.URL == "" {
			return fmt.Errorf("a mirror must specify a repository URL")
		}
	}

	w, err := sh.writerAt(installPath)
	if err != nil {
		return err
	}
	defer w.Close()
	b, err := json.MarshalIndent(config, "", "  ")
	if err != nil {
		return err
	}
	if _, err := w.Write(b); err != nil {
		return err
	}
	logger.Debugf(ctx, "successfully wrote repo config to %q:\n%s\n", installPath, b)
	return sh.run(ctx, repoAddCmd(installPath))
}

func repoAddCmd(file string) []string {
	return []string{"pkgctl", "repo", "add", "--file", file}
}

type shell interface {
	writerAt(string) (io.WriteCloser, error)
	run(context.Context, []string) error
}

type remoteShell struct {
	sshClient  *sshutil.Client
	sftpClient *sftp.Client
}

func newRemoteShell(sshClient *sshutil.Client) (*remoteShell, error) {
	sftpClient, err := sshClient.NewSFTPClient()
	if err != nil {
		return nil, err
	}
	return &remoteShell{
		sshClient:  sshClient,
		sftpClient: sftpClient,
	}, nil
}

func (sh remoteShell) writerAt(remote string) (io.WriteCloser, error) {
	return sh.sftpClient.Create(remote)
}

func (sh remoteShell) run(ctx context.Context, cmd []string) error {
	return sh.sshClient.Run(ctx, cmd, os.Stdout, os.Stderr)
}

// GetRootKeysInsecurely returns the list of public key config objects from a package
// repository. Note this is an insecure method, as it leaves the caller open to a
// man-in-the-middle attack.
func GetRootKeysInsecurely(ctx context.Context, repoURL string) ([]KeyConfig, error) {
	root, err := getRepoRoot(ctx, repoURL)
	if err != nil {
		return nil, fmt.Errorf("failed to fetch %s: %v", rootJSON, err)
	}
	return GetRootKeys(root)
}

func getRepoRoot(ctx context.Context, repoURL string) (*tuf_data.Root, error) {
	url := fmt.Sprintf("%s/%s", repoURL, rootJSON)
	var resp *http.Response
	b := retry.WithMaxAttempts(retry.NewExponentialBackoff(backoffFloor, backoffCeiling, backoffMultiplier), 5)
	err := retry.Retry(ctx, b, func() error {
		var err error
		resp, err = http.Get(url)
		return err
	}, nil)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		dump, err := httputil.DumpResponse(resp, true)
		if err != nil {
			return nil, fmt.Errorf("failed to dump %s response: %w", rootJSON, err)
		}
		logger.Debugf(ctx, "%s response dump: %s", rootJSON, dump)
		return nil, fmt.Errorf("received a non-200 %s response: %d", rootJSON, resp.StatusCode)
	}

	var signed tuf_data.Signed
	if err := json.NewDecoder(resp.Body).Decode(&signed); err != nil {
		return nil, err
	}
	var root tuf_data.Root
	err = json.Unmarshal(signed.Signed, &root)
	return &root, err
}
