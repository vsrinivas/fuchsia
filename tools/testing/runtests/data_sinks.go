// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package runtests

import (
	"encoding/json"
	"io"
	"os"
	"path"
	"path/filepath"

	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"
)

// DataSinkCopier copies data sinks from a remote host after a runtests invocation.
type DataSinkCopier struct {
	viewer remoteViewer
}

// NewDataSinkCopier constructs a copier using the specified ssh client.
func NewDataSinkCopier(client *ssh.Client) (*DataSinkCopier, error) {
	sftpClient, err := sftp.NewClient(client)
	if err != nil {
		return nil, err
	}
	viewer := &sftpViewer{sftpClient}
	return &DataSinkCopier{viewer: viewer}, nil
}

// Copy copies data sinks using the copier's remote viewer.
func (c DataSinkCopier) Copy(remoteDir, localDir string) (DataSinkMap, error) {
	return copyDataSinks(c.viewer, remoteDir, localDir)
}

func (c DataSinkCopier) Close() error {
	return c.viewer.close()
}

// remoteView provides an interface for fetching a summary.json and copying
// files from a remote host after a runtests invocation.
type remoteViewer interface {
	summary(string) (*TestSummary, error)
	copyFile(string, string) error
	close() error
}

type sftpViewer struct {
	client *sftp.Client
}

func (v sftpViewer) summary(summaryPath string) (*TestSummary, error) {
	f, err := v.client.Open(summaryPath)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var summary TestSummary
	if err = json.NewDecoder(f).Decode(&summary); err != nil {
		return nil, err
	}
	return &summary, nil
}

func (v sftpViewer) copyFile(remote, local string) error {
	remoteFile, err := v.client.Open(remote)
	if err != nil {
		return err
	}
	defer remoteFile.Close()

	if err = os.MkdirAll(filepath.Dir(local), 0777); err != nil {
		return err
	}
	localFile, err := os.Create(local)
	if err != nil {
		return err
	}
	defer localFile.Close()

	_, err = io.Copy(localFile, remoteFile)
	return err
}

func (v sftpViewer) close() error {
	return v.client.Close()
}

// CopyDataSinks retrieves the summary.json produced by runtests (assuming only
// a single test was run) and copies the data sinks specified in the summary
// from the remoteOutputDir on the target to the localOutputDir on the host. It
// modifies the output data sinks in-place, referencing the local paths of the
// copied files.
func copyDataSinks(viewer remoteViewer, remoteOutputDir, localOutputDir string) (DataSinkMap, error) {
	summaryPath := path.Join(remoteOutputDir, TestSummaryFilename)
	summary, err := viewer.summary(summaryPath)
	if err != nil {
		return nil, err
	}

	sinks := DataSinkMap{}
	for _, details := range summary.Tests {
		for name, files := range details.DataSinks {
			var outputFiles []DataSink
			for _, file := range files {
				src := path.Join(remoteOutputDir, file.File)
				dest := filepath.Join(localOutputDir, file.File)
				if err = viewer.copyFile(src, dest); err != nil {
					return nil, err
				}
				outputFiles = append(outputFiles, DataSink{
					Name: file.Name,
					File: dest,
				})
			}
			sinks[name] = outputFiles
		}
	}
	return sinks, nil
}
