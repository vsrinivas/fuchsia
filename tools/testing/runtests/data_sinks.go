// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package runtests

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path"
	"path/filepath"

	"github.com/pkg/sftp"

	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
)

// DataSinkCopier copies data sinks from a remote host after a runtests invocation.
type DataSinkCopier struct {
	viewer    remoteViewer
	sshClient *sshutil.Client
}

// NewDataSinkCopier constructs a copier using the specified ssh client.
func NewDataSinkCopier(client *sshutil.Client) (*DataSinkCopier, error) {
	sftpClient, err := client.NewSFTPClient()
	if err != nil {
		return nil, err
	}
	viewer := &sftpViewer{sftpClient}
	copier := &DataSinkCopier{
		viewer:    viewer,
		sshClient: client,
	}
	return copier, nil
}

// Copy copies data sinks using the copier's remote viewer.
func (c DataSinkCopier) Copy(references []DataSinkReference, localDir string) (DataSinkMap, error) {
	return copyDataSinks(c.viewer, references, localDir)
}

// GetReferences returns a map of test name to a reference to the remote data sinks.
func (c DataSinkCopier) GetReferences(remoteDir string) (map[string]DataSinkReference, error) {
	return getDataSinkReferences(c.viewer, remoteDir)
}

// Reconnect should be called after the sshClient has been disconnected and
// reconnected. It closes the old viewer and creates a new viewer using the
// refreshed sshClient.
func (c *DataSinkCopier) Reconnect() error {
	// This may fail because the underlying ssh session has already been
	// closed, which is fine and expected, so no need to check the
	// returned error.
	c.viewer.close()

	sftpClient, err := c.sshClient.NewSFTPClient()
	if err != nil {
		return fmt.Errorf("failed to create new SFTP client: %w", err)
	}
	c.viewer = &sftpViewer{sftpClient}
	return nil
}

func (c DataSinkCopier) Close() error {
	return c.viewer.close()
}

// DataSinkReference holds information about data sinks on the target.
type DataSinkReference struct {
	Sinks     DataSinkMap
	RemoteDir string
}

// Size returns the number of sinks held by the reference.
func (d DataSinkReference) Size() int {
	numSinks := 0
	for _, files := range d.Sinks {
		numSinks += len(files)
	}
	return numSinks
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

	if err = os.MkdirAll(filepath.Dir(local), 0o777); err != nil {
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

// GetDataSinkReferences retrieves the summary.json written to the
// `remoteOutputDir` and gets the data sinks specified in the summary.
func getDataSinkReferences(viewer remoteViewer, remoteOutputDir string) (map[string]DataSinkReference, error) {
	sinksPerTest := make(map[string]DataSinkReference)
	summaryPath := path.Join(remoteOutputDir, TestSummaryFilename)
	summary, err := viewer.summary(summaryPath)
	if err != nil {
		return sinksPerTest, fmt.Errorf("failed to read test summary from %q: %w", summaryPath, err)
	}

	for _, details := range summary.Tests {
		sinksPerTest[details.Name] = DataSinkReference{details.DataSinks, remoteOutputDir}
	}
	return sinksPerTest, nil
}

// CopyDataSinks copies the data sinks specified in references from the
// remoteOutputDir on the target to the localOutputDir on the host.
// It returns a DataSinkMap of the copied files, removing duplicates across
// the references.
func copyDataSinks(viewer remoteViewer, references []DataSinkReference, localOutputDir string) (DataSinkMap, error) {
	sinks := DataSinkMap{}
	copied := make(map[string]struct{})
	for _, ref := range references {
		for name, files := range ref.Sinks {
			if _, ok := sinks[name]; !ok {
				sinks[name] = []DataSink{}
			}
			for _, file := range files {
				if _, ok := copied[file.File]; ok {
					continue
				}
				src := path.Join(ref.RemoteDir, file.File)
				dest := filepath.Join(localOutputDir, file.File)
				if err := viewer.copyFile(src, dest); err != nil {
					return nil, fmt.Errorf("failed to copy data sink %q from %s: %w", file.File, ref.RemoteDir, err)
				}
				copied[file.File] = struct{}{}
				sinks[name] = append(sinks[name], file)
			}
		}
	}
	return sinks, nil
}
