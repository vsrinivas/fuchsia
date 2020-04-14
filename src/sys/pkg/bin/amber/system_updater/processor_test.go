// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_updater

import (
	"bytes"
	"reflect"
	"testing"
)

type byteReadCloser struct {
	*bytes.Reader
}

func newByteReadCloser(d []byte) *byteReadCloser {
	return &byteReadCloser{bytes.NewReader(d)}
}

func (b *byteReadCloser) Close() error {
	return nil
}

func TestParsePackagesLineFormatted(t *testing.T) {
	expectedPkgs := [2]string{
		"fuchsia-pkg://fuchsia.com/amber/0?hash=abcdef",
		"fuchsia-pkg://fuchsia.com/pkgfs/0?hash=123456789",
	}

	pFile := newByteReadCloser([]byte("amber/0=abcdef\npkgfs/0=123456789"))
	pkgs, err := ParsePackagesLineFormatted(pFile)
	if err != nil {
		t.Fatalf("Error processing packages: %s", err)
	}

	if len(expectedPkgs) != len(pkgs) {
		t.Logf("Length of parsed packages != expected")
		t.Fail()
	}

	for i, pkgURI := range pkgs {
		if expectedPkgs[i] != pkgURI {
			t.Fail()
			t.Logf("Expected URI does not match, expected %q, found %q", expectedPkgs[i], pkgURI)
		}
	}
}

func TestParsePackagesJson(t *testing.T) {
	expectedPkgs := [2]string{
		"fuchsia-pkg://fuchsia.com/amber/0?hash=abcdef",
		"fuchsia-pkg://fuchsia.com/pkgfs/0?hash=123456789",
	}

	pFile := newByteReadCloser([]byte(`
		{
			"version": 1,
			"content": [
				"fuchsia-pkg://fuchsia.com/amber/0?hash=abcdef",
				"fuchsia-pkg://fuchsia.com/pkgfs/0?hash=123456789"
				]
		}
	`))

	pkgs, err := ParsePackagesJson(pFile)
	if err != nil {
		t.Fatalf("Error processing packages: %s", err)
	}

	if len(expectedPkgs) != len(pkgs) {
		t.Logf("Length of parsed packages != expected")
		t.Fail()
	}

	for i, pkgURI := range pkgs {
		if expectedPkgs[i] != pkgURI {
			t.Fail()
			t.Logf("Expected URI does not match, expected %q, found %q", expectedPkgs[i], pkgURI)
		}
	}
}

// Verifies that ParseImages() using the given image file and update package
// contents produces the expected image list.
func VerifyParseImages(t *testing.T, imageFileContents string, updatePackageFiles []string, expectedImages []Image) {
	iFile := newByteReadCloser([]byte(imageFileContents))

	images, err := ParseImages(iFile, updatePackageFiles)
	if err != nil {
		t.Fatalf("Error parsing images: %s", err)
	}

	if !reflect.DeepEqual(expectedImages, images) {
		t.Fail()
		t.Logf("Expected %+v but got %+v", expectedImages, images)
	}
}

func TestParseImages(t *testing.T) {
	imageFileContents := "dc38ffa1029c3fd44\n"
	updatePackageFiles := []string{}
	expectedImages := []Image{
		// Untyped files don't care whether they exist in the package or not.
		{Name: "dc38ffa1029c3fd44", Type: ""},
	}

	VerifyParseImages(t, imageFileContents, updatePackageFiles, expectedImages)
}

func TestParseImagesWithType(t *testing.T) {
	imageFileContents := "firmware[_type]\n"
	updatePackageFiles := []string{"firmware", "firmware_abc", "firmware_123"}
	expectedImages := []Image{
		{Name: "firmware", Type: ""},
		{Name: "firmware", Type: "abc"},
		{Name: "firmware", Type: "123"},
	}

	VerifyParseImages(t, imageFileContents, updatePackageFiles, expectedImages)
}

func TestParseImagesTypeRequiresUnderscore(t *testing.T) {
	imageFileContents := "firmware[_type]\n"
	updatePackageFiles := []string{"firmware_a", "firmware2"}
	expectedImages := []Image{
		{Name: "firmware", Type: "a"},
		// firmware2 doesn't follow the <base>_<type> format, should be ignored.
	}

	VerifyParseImages(t, imageFileContents, updatePackageFiles, expectedImages)
}

func TestParseImagesTypeMultipleUnderscore(t *testing.T) {
	imageFileContents := "firmware[_type]\n"
	updatePackageFiles := []string{"firmware_a_b", "firmware_firmware_2"}
	expectedImages := []Image{
		{Name: "firmware", Type: "a_b"},
		{Name: "firmware", Type: "firmware_2"},
	}

	VerifyParseImages(t, imageFileContents, updatePackageFiles, expectedImages)
}

func TestParseImagesEmptyType(t *testing.T) {
	imageFileContents := "firmware[_type]\n"
	updatePackageFiles := []string{"firmware_"}
	expectedImages := []Image{
		{Name: "firmware", Type: ""},
	}

	VerifyParseImages(t, imageFileContents, updatePackageFiles, expectedImages)
}

func TestParseImagesNoTypeMatches(t *testing.T) {
	imageFileContents := "firmware[_type]\n"
	updatePackageFiles := []string{"not_firmware"}
	expectedImages := []Image{}

	VerifyParseImages(t, imageFileContents, updatePackageFiles, expectedImages)
}

func TestParseImagesUntypedWithUnderscore(t *testing.T) {
	imageFileContents := "untyped_firmware\n"
	updatePackageFiles := []string{"untyped_firmware_2"}
	expectedImages := []Image{
		{Name: "untyped_firmware", Type: ""},
	}

	VerifyParseImages(t, imageFileContents, updatePackageFiles, expectedImages)
}

func TestImageFilenameWithoutType(t *testing.T) {
	image := Image{Name: "name", Type: ""}
	if image.Filename() != "name" {
		t.Fail()
		t.Logf("Expected %q, got %q\n", "name", image.Filename())
	}
}

func TestImageFilenameWithType(t *testing.T) {
	image := Image{Name: "name", Type: "type"}
	if image.Filename() != "name_type" {
		t.Fail()
		t.Logf("Expected %q, got %q\n", "name_type", image.Filename())
	}
}
