// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

// +build !fuchsia

package repo

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"

	tuf_data "github.com/flynn/go-tuf/data"
	"github.com/pkg/sftp"

	"go.fuchsia.dev/fuchsia/tools/lib/retry"

	"golang.org/x/crypto/ssh"
)

const (
	repoAddCmdTemplate = "pkgctl repo add --file %s"
	defaultInstallPath = "/tmp/config.json"
	rootJSON           = "root.json"
)

// AddInsecurely configures a provided repository as an update source, though
// offers weaker security guarantees than AddFromConfig (e.g., by allowing a
// man-in-the-middle attack during the fetching of root keys).
func AddInsecurely(client *ssh.Client, repoID, repoURL, blobURL string) error {
	root, err := getRepoRoot(repoURL)
	if err != nil {
		return fmt.Errorf("failed to fetch %s: %v", rootJSON, err)
	}
	rootKeys, err := GetRootKeys(root)
	if err != nil {
		return fmt.Errorf("failed to derive public root keys: %s", err)
	}
	cfg := &Config{
		URL:      repoID,
		RootKeys: rootKeys,
		Mirrors: []MirrorConfig{
			{
				URL:     repoURL,
				BlobURL: blobURL,
			},
		},
	}
	return AddFromConfig(client, cfg)
}

// AddFromConfig writes the given config to the given remote install path and
// adds it as an update source.
func AddFromConfig(client *ssh.Client, config *Config) error {
	sh, err := newRemoteShell(client)
	if err != nil {
		return err
	}
	defer func() {
		if err := sh.sftpClient.Remove(defaultInstallPath); err != nil {
			log.Printf("error: failed to remove %q: %v", defaultInstallPath, err)
		}
		if err := sh.sftpClient.Close(); err != nil {
			log.Printf("error: failed to close SFTP client: %v", err)
		}
	}()
	return addFromConfig(config, sh, defaultInstallPath)
}

func addFromConfig(config *Config, sh shell, installPath string) error {
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
	log.Printf("successfully wrote repo config to %q:\n%s\n", installPath, b)
	return sh.run(repoAddCmd(installPath))
}

func repoAddCmd(file string) string {
	return fmt.Sprintf(repoAddCmdTemplate, file)
}

type shell interface {
	writerAt(string) (io.WriteCloser, error)
	run(string) error
}

type remoteShell struct {
	sshClient  *ssh.Client
	sftpClient *sftp.Client
}

func newRemoteShell(sshClient *ssh.Client) (*remoteShell, error) {
	sftpClient, err := sftp.NewClient(sshClient)
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

func (sh remoteShell) run(cmd string) error {
	session, err := sh.sshClient.NewSession()
	if err != nil {
		return err
	}
	session.Stdout = os.Stdout
	session.Stderr = os.Stderr
	defer session.Close()
	log.Print("running command: ", cmd)
	return session.Run(cmd)
}

func getRepoRoot(repoURL string) (*tuf_data.Root, error) {
	url := fmt.Sprintf("%s/%s", repoURL, rootJSON)
	var resp *http.Response
	b := retry.WithMaxRetries(&retry.ZeroBackoff{}, 5)
	err := retry.Retry(context.Background(), b, func() error {
		var err error
		resp, err = http.Get(url)
		return err
	}, nil)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	var signed tuf_data.Signed
	if err := json.NewDecoder(resp.Body).Decode(&signed); err != nil {
		return nil, err
	}
	var root tuf_data.Root
	err = json.Unmarshal(signed.Signed, &root)
	return &root, err
}
