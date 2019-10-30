// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package repo

import (
	"encoding/json"
	"fmt"
	"io"

	"github.com/pkg/sftp"

	"golang.org/x/crypto/ssh"
)

const (
	repoAddCmdTemplate = "pkgctl repo add --file %s"
)

// Configurer is used to remotely configure a package repository.
type Configurer struct {
	shell shell
}

// NewConfigurer returns a configurer, given an SSH client.
func NewConfigurer(client *ssh.Client) (*Configurer, error) {
	sh, err := newRemoteShell(client)
	if err != nil {
		return nil, err
	}
	return &Configurer{shell: sh}, nil
}

func (c *Configurer) Close() error {
	return c.shell.close()
}

// Configure writes the given config to the associated remote filesystem
// and adds it as an update source.
func (c Configurer) Configure(config *RepositoryConfig, remote string) error {
	w, err := c.shell.writerAt(remote)
	if err != nil {
		return err
	}
	defer w.Close()

	b, err := json.Marshal(config)
	if err != nil {
		return err
	}
	if _, err := w.Write(b); err != nil {
		return err
	}
	return c.shell.run(repoAddCmd(remote))
}

func repoAddCmd(file string) string {
	return fmt.Sprintf(repoAddCmdTemplate, file)
}

type shell interface {
	writerAt(string) (io.WriteCloser, error)
	run(string) error
	close() error
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
	defer session.Close()
	return session.Run(cmd)
}

func (sh remoteShell) close() error {
	if err := sh.sshClient.Close(); err != nil {
		sh.sftpClient.Close()
		return err
	}
	return sh.sftpClient.Close()
}
