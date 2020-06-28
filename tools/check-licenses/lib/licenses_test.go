// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"os"
	"testing"
)

func TestLicensesMatchSingleLicenseFile(t *testing.T) {
	// TODO(solomonkinard) implement
}

func TestLicensesMatchFile(t *testing.T) {
	// TODO(solomonkinard) implement
}

func TestLicensesInit(t *testing.T) {
	folder := "golden"
	filename := "test.lic"
	path := folder + "/" + filename
	var licenses Licenses
	os.Mkdir(folder, 0700)
	{
		f, err := os.Create(path)
		if err != nil {
			defer f.Close()
		}
		f.WriteString("abc")
	}
	if err := licenses.Init(folder); err != nil {
		t.Error("error: licenses.Init()")
	}
	os.Remove(path)
	os.Remove(folder)
}

func TestLicensesNew(t *testing.T) {
	folder := "golden"
	filename := "test.lic"
	path := folder + "/" + filename
	os.Mkdir(folder, 0700)
	{
		f, err := os.Create(path)
		if err != nil {
			defer f.Close()
		}
		f.WriteString("abc")
	}
	_, err := NewLicenses(folder)
	if err != nil {
		t.Error("error: NewLicenses(...)")
	}
	os.Remove(path)
	os.Remove(folder)
}

func TestLicensesWorker(t *testing.T) {
	// TODO(solomonkinard) implement
}
