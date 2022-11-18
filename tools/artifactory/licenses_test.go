// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"bytes"
	"compress/gzip"
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/build"
)

// Implements licModules
type mockLicenseModules struct {
	lics     []build.License
	buildDir string
}

func (m mockLicenseModules) BuildDir() string {
	return m.buildDir
}

func (m mockLicenseModules) Licenses() []build.License {
	return m.lics
}

func TestLicenseUploads(t *testing.T) {
	dir := t.TempDir()

	namespace := "namespace"

	licenseFilesDir := "license_files"
	licenseFilesDirAbs := filepath.Join(dir, licenseFilesDir)
	if err := os.MkdirAll(licenseFilesDirAbs, 0755); err != nil {
		t.Fatalf("failed to create LicenseFiles directory: %s", err)
	}
	name := filepath.Join(licenseFilesDirAbs, "LICENSE")
	content := []byte("Hello World!")
	if err := os.WriteFile(name, content, 0o600); err != nil {
		t.Fatalf("failed to write to fake LICENSE file: %s", err)
	}

	runFilesArchive := "runfiles.gz"
	runFilesArchiveAbs := filepath.Join(dir, runFilesArchive)
	if err := os.MkdirAll(runFilesArchiveAbs, 0755); err != nil {
		t.Fatalf("failed to create RunFiles archive: %s", err)
	}
	content = []byte("{\"logLevel\": 0}")
	buf := bytes.Buffer{}
	zw := gzip.NewWriter(&buf)
	if _, err := zw.Write(content); err != nil {
		t.Fatalf("failed to create fake runFilesArchive: %s", err)
	}
	if err := zw.Close(); err != nil {
		t.Fatalf("failed to close fake runFilesArchive: %s", err)
	}
	if err := os.WriteFile(runFilesArchive, buf.Bytes(), 0o600); err != nil {
		t.Fatalf("failed to write to fake runFilesArchive: %s", err)
	}

	complianceFile := "compliance.csv"
	complianceFileAbs := filepath.Join(dir, complianceFile)
	content = []byte("LICENSE,type,url,etc")
	if err := os.WriteFile(complianceFileAbs, content, 0o600); err != nil {
		t.Fatalf("failed to write to fake compliance file: %s", err)
	}

	m := &mockLicenseModules{
		buildDir: dir,
		lics: []build.License{
			{
				ComplianceFile:  complianceFile,
				LicenseFilesDir: licenseFilesDir,
				RunFilesArchive: runFilesArchive,
			},
		},
	}
	want := []Upload{
		{
			Source:      complianceFileAbs,
			Destination: filepath.Join(namespace, complianceFile),
			Deduplicate: false,
		},
		{
			Source:      licenseFilesDirAbs,
			Destination: filepath.Join(namespace, "texts"),
			Deduplicate: false,
			Recursive:   true,
		},
		{
			Source:      runFilesArchiveAbs,
			Destination: filepath.Join(namespace, runFilesArchive),
		},
	}
	got, err := licenseUploads(m, namespace)
	if err != nil {
		t.Fatalf("licenseUploads failed: %s", err)
	}
	if diff := cmp.Diff(want, got); diff != "" {
		t.Fatalf("unexpected license uploads (-want +got):\n%s", diff)
	}
}
