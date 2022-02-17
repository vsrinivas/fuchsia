// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package project

import (
	"flag"
	"path/filepath"
	"testing"
)

var testDataDir = flag.String("test_data_dir", "", "Path to test data directory")

func TestEmptyReadmeFile(t *testing.T) {
	path := filepath.Join(*testDataDir, "README.empty")
	_, err := NewProject(path)
	if err == nil {
		t.Errorf("%v: expected error, got nil.", t.Name())
	}
}

func TestEmptyNameField(t *testing.T) {
	path := filepath.Join(*testDataDir, "README.noname")
	_, err := NewProject(path)
	if err == nil {
		t.Errorf("%v: expected error, got nil.", t.Name())
	}
}

func TestEmptyLicenseField(t *testing.T) {
	path := filepath.Join(*testDataDir, "README.nolicense")
	_, err := NewProject(path)
	if err == nil {
		t.Errorf("%v: expected error, got nil.", t.Name())
	}
}

func TestMissingLicenseFile(t *testing.T) {
	path := filepath.Join(*testDataDir, "README.missinglicense")
	_, err := NewProject(path)
	if err == nil {
		t.Errorf("%v: expected error, got nil.", t.Name())
	}
}

func TestIncorrectFormat(t *testing.T) {
	path := filepath.Join(*testDataDir, "README.incorrectformat")
	_, err := NewProject(path)
	if err == nil {
		t.Errorf("%v: expected error, got nil.", t.Name())
	}
}

func TestNameLicenseProvided(t *testing.T) {
	name := "Test Readme Project"
	licenseFile := "README.happy"

	path := filepath.Join(*testDataDir, "README.happy")
	p, err := NewProject(path)
	if err != nil {
		t.Errorf("%v: expected no error, got %v.", t.Name(), err)
	}
	if p.Name != name {
		t.Errorf("%v: expected Name == \"%v\", got %v.", t.Name(), name, p.Name)
	}
	if len(p.LicenseFile) != 1 || p.LicenseFile[0] != licenseFile {
		t.Errorf("%v: expected License file == \"%v\", got %v.", t.Name(), licenseFile, p.LicenseFile)
	}
}

func TestMultiLineFields(t *testing.T) {
	name := "Test Readme Project"
	licenseFile := "README.multiline"
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

	path := filepath.Join(*testDataDir, "README.multiline")
	p, err := NewProject(path)
	if err != nil {
		t.Errorf("%v: expected no error, got %v.", t.Name(), err)
	}
	if p.Name != name {
		t.Errorf("%v: expected Name == \"%v\", got %v.", t.Name(), name, p.Name)
	}
	if len(p.LicenseFile) != 1 || p.LicenseFile[0] != licenseFile {
		t.Errorf("%v: expected License file == \"%v\", got %v.", t.Name(), licenseFile, p.LicenseFile)
	}
	if p.Description != description {
		t.Errorf("%v: expected Description == \"%v\", got %v.", t.Name(), description, p.Description)
	}
	if p.License != license {
		t.Errorf("%v: expected License == \"%v\", got %v.", t.Name(), license, p.License)
	}
	if p.LocalModifications != localmods {
		t.Errorf("%v: expected Local Modifications == \"%v\", got %v.", t.Name(), localmods, p.LocalModifications)
	}
	if p.Version != version {
		t.Errorf("%v: expected Version == \"%v\", got %v.", t.Name(), version, p.Version)
	}
}
