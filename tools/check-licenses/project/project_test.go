// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package project

import (
	"flag"
	"path/filepath"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
)

var testDataDir = flag.String("test_data_dir", "", "Path to test data directory")

func TestEmptyReadmeFile(t *testing.T) {
	setup()
	path := filepath.Join(*testDataDir, "empty", "README.fuchsia")
	_, err := NewProject(path, filepath.Dir(path))
	if err == nil {
		t.Errorf("%v: expected error, got nil.", t.Name())
	}
}

func TestEmptyNameField(t *testing.T) {
	setup()
	path := filepath.Join(*testDataDir, "noname", "README.fuchsia")
	_, err := NewProject(path, filepath.Dir(path))
	if err == nil {
		t.Errorf("%v: expected error, got nil.", t.Name())
	}
}

func TestEmptyLicenseField(t *testing.T) {
	setup()
	path := filepath.Join(*testDataDir, "nolicense", "README.fuchsia")
	_, err := NewProject(path, filepath.Dir(path))
	if err == nil {
		t.Errorf("%v: expected error, got nil.", t.Name())
	}
}

func TestMissingLicenseFile(t *testing.T) {
	setup()
	path := filepath.Join(*testDataDir, "missinglicense", "README.fuchsia")
	_, err := NewProject(path, filepath.Dir(path))
	if err == nil {
		t.Errorf("%v: expected error, got nil.", t.Name())
	}
}

func TestIncorrectFormat(t *testing.T) {
	setup()
	path := filepath.Join(*testDataDir, "incorrectformat", "README.fuchsia")
	_, err := NewProject(path, filepath.Dir(path))
	if err == nil {
		t.Errorf("%v: expected error, got nil.", t.Name())
	}
}

func TestNameLicenseProvided(t *testing.T) {
	setup()
	name := "Test Readme Project"
	licenseFile := "README.fuchsia"

	path := filepath.Join(*testDataDir, "happy", "README.fuchsia")
	p, err := NewProject(path, filepath.Dir(path))
	if err != nil {
		t.Errorf("%v: expected no error, got %v.", t.Name(), err)
	}
	if p.Name != name {
		t.Errorf("%v: expected Name == \"%v\", got %v.", t.Name(), name, p.Name)
	}
	if len(p.LicenseFile) != 1 || p.LicenseFile[0].Name != licenseFile {
		t.Errorf("%v: expected License file == \"%v\", got %v.", t.Name(), licenseFile, p.LicenseFile[0].Name)
	}
}

func TestMultiLineFields(t *testing.T) {
	setup()

	name := "Test Readme Project"
	licenseFile := "README.fuchsia"
	description := "\n"
	description += "\n"
	description += "This is a test of the multiline description field.\n"
	description += "This text will hopefully be parsed correctly.\n"
	description += "Test Test.\n"
	description += "\n"
	license := "Proprietary"
	localmods := "\n"
	localmods += "Added some stuff\n"
	localmods += "Did some other stuff too\n"
	localmods += "\n"
	version := "2.0"

	path := filepath.Join(*testDataDir, "multiline", "README.fuchsia")
	p, err := NewProject(path, filepath.Dir(path))
	if err != nil {
		t.Errorf("%v: expected no error, got %v.", t.Name(), err)
	}
	if p.Name != name {
		t.Errorf("%v: expected Name == \"%v\", got %v.", t.Name(), name, p.Name)
	}
	if len(p.LicenseFile) != 1 || p.LicenseFile[0].Name != licenseFile {
		t.Errorf("%v: expected License file == \"%v\", got %v.", t.Name(), licenseFile, p.LicenseFile[0].Name)
	}
	if strings.TrimSpace(p.Description) != strings.TrimSpace(description) {
		t.Errorf("%v: expected Description == \"%v\", got %v.", t.Name(), description, p.Description)
	}
	if p.License != license {
		t.Errorf("%v: expected License == \"%v\", got %v.", t.Name(), license, p.License)
	}
	if strings.TrimSpace(p.LocalModifications) != strings.TrimSpace(localmods) {
		t.Errorf("%v: expected Local Modifications == \"%v\", got %v.", t.Name(), localmods, p.LocalModifications)
	}
	if p.Version != version {
		t.Errorf("%v: expected Version == \"%v\", got %v.", t.Name(), version, p.Version)
	}
}

func setup() {
	file.Config = file.NewFileConfig()
	Config = NewProjectConfig()
}
