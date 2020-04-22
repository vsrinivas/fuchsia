// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_updater

import (
	"bytes"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
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
		t.Errorf("Length of parsed packages != expected")
	}

	for i, pkgURI := range pkgs {
		if expectedPkgs[i] != pkgURI {
			t.Errorf("Expected URI does not match, expected %q, found %q", expectedPkgs[i], pkgURI)
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
			"version": "1",
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
		t.Errorf("Length of parsed packages != expected")
	}

	for i, pkgURI := range pkgs {
		if expectedPkgs[i] != pkgURI {
			t.Errorf("Expected URI does not match, expected %q, found %q", expectedPkgs[i], pkgURI)
		}
	}
}

// Verifies that ParseImages() using the given image file and update package
// contents produces the expected image list.
func TestParseImages(t *testing.T) {
	for _, tc := range []struct {
		name               string
		imageFileContents  string
		updatePackageFiles []string
		expectedImages     []Image
	}{
		{
			name:               "parse images",
			imageFileContents:  "dc38ffa1029c3fd44\n",
			updatePackageFiles: []string{},
			expectedImages: []Image{
				// Untyped files don't care whether they exist in the package or not.
				{Name: "dc38ffa1029c3fd44", Type: ""},
			},
		},
		{
			name:               "parse images with type",
			imageFileContents:  "firmware[_type]\n",
			updatePackageFiles: []string{"firmware", "firmware_abc", "firmware_123"},
			expectedImages: []Image{
				{Name: "firmware", Type: ""},
				{Name: "firmware", Type: "abc"},
				{Name: "firmware", Type: "123"},
			},
		},
		{
			name:               "parse images type requires underscores",
			imageFileContents:  "firmware[_type]\n",
			updatePackageFiles: []string{"firmware_a", "firmware2"},
			expectedImages: []Image{
				{Name: "firmware", Type: "a"},
				// firmware2 doesn't follow the <base>_<type> format, should be ignored.
			},
		},
		{
			name:               "parse images type multiple underscores",
			imageFileContents:  "firmware[_type]\n",
			updatePackageFiles: []string{"firmware_a_b", "firmware_firmware_2"},
			expectedImages: []Image{
				{Name: "firmware", Type: "a_b"},
				{Name: "firmware", Type: "firmware_2"},
			},
		},
		{
			name:               "parse images empty type",
			imageFileContents:  "firmware[_type]\n",
			updatePackageFiles: []string{"firmware_"},
			expectedImages: []Image{
				{Name: "firmware", Type: ""},
			},
		},
		{
			name:               "parse images no type matches",
			imageFileContents:  "firmware[_type]\n",
			updatePackageFiles: []string{"not_firmware"},
			expectedImages:     []Image{},
		},
		{
			name:               "parse images untyped with underscore",
			imageFileContents:  "untyped_firmware\n",
			updatePackageFiles: []string{"untyped_firmware_2"},
			expectedImages: []Image{
				{Name: "untyped_firmware", Type: ""},
			},
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			iFile := newByteReadCloser([]byte(tc.imageFileContents))

			images, err := ParseImages(iFile, tc.updatePackageFiles)
			if err != nil {
				t.Fatalf("Error parsing images: %s", err)
			}

			if !reflect.DeepEqual(tc.expectedImages, images) {
				t.Errorf("Expected %+v but got %+v", tc.expectedImages, images)
			}

		})
	}
}

func TestImageFilenameWithoutType(t *testing.T) {
	image := Image{Name: "name", Type: ""}
	if image.Filename() != "name" {
		t.Errorf("Expected %q, got %q\n", "name", image.Filename())
	}
}

func TestImageFilenameWithType(t *testing.T) {
	image := Image{Name: "name", Type: "type"}
	if image.Filename() != "name_type" {
		t.Errorf("Expected %q, got %q\n", "name_type", image.Filename())
	}
}

func genValidUpdateModeJson(mode string) string {
	return fmt.Sprintf(`
		{
			"version": "1",
			"content": {
				"mode": "%s"
			}
		}
	`, mode)
}

func TestParseUpdateMode(t *testing.T) {
	for _, tc := range []struct {
		name         string
		modeFileData []byte
		expectedMode UpdateMode
		errorTarget  error
	}{
		{
			name:         "success normal",
			modeFileData: []byte(genValidUpdateModeJson("normal")),
			expectedMode: UpdateModeNormal,
		},
		{
			name:         "success force-recovery",
			modeFileData: []byte(genValidUpdateModeJson("force-recovery")),
			expectedMode: UpdateModeForceRecovery,
		},
		{
			name:         "success update-mode file does not exist",
			modeFileData: nil, // since data is nil, we won't write an update-mode file.
			expectedMode: UpdateModeNormal,
		},
		{
			name:         "fail update mode not supported",
			modeFileData: []byte(genValidUpdateModeJson("potato")),
			expectedMode: "",
			errorTarget:  updateModeNotSupportedError(""),
		},
		{
			name:         "fail json bad formatting",
			modeFileData: []byte("invalid-json"),
			expectedMode: "",
			errorTarget:  jsonUnmarshalError{fmt.Errorf("")},
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			// Generate update package based on test inputs.
			dirPath, err := ioutil.TempDir("", "update")
			if err != nil {
				t.Fatalf("Unable to create tempdir for update package: %v", err)
			}
			defer os.RemoveAll(dirPath)
			if tc.modeFileData != nil {
				if err := ioutil.WriteFile(filepath.Join(dirPath, "update-mode"), tc.modeFileData, 0666); err != nil {
					t.Fatalf("Unable to write update-mode file to tempdir: %v", err)
				}
			}
			dir, err := os.Open(dirPath)
			if err != nil {
				t.Fatalf("Unable to open tempdir: %v", err)
			}
			updatePackage := UpdatePackage{dir}

			// Parse update mode from the package.
			mode, err := ParseUpdateMode(&updatePackage)

			// Verify expected outcome.
			if got, want := reflect.TypeOf(err), reflect.TypeOf(tc.errorTarget); got != want {
				t.Fatalf("ParseUpdateMode() got error type %v, want %v", got, want)
			}
			if mode != tc.expectedMode {
				t.Fatalf("ParseUpdateMode() got mode %q, want %q", mode, tc.expectedMode)
			}

		})
	}
}

func TestValidateImages(t *testing.T) {
	for _, tc := range []struct {
		name       string
		images     []Image
		updateMode UpdateMode
		wantError  bool
	}{
		{
			name:       "success normal update package has zbi",
			images:     []Image{{Name: "zbi"}},
			updateMode: UpdateModeNormal,
			wantError:  false,
		},
		{
			name:       "success force-recovery update package does not have zbi",
			images:     []Image{},
			updateMode: UpdateModeForceRecovery,
			wantError:  false,
		},
		{
			name:       "failure normal update package does not have zbi",
			images:     []Image{},
			updateMode: UpdateModeNormal,
			wantError:  true,
		},
		{
			name:       "failure force-recovery update package has zbi",
			images:     []Image{{Name: "zbi"}},
			updateMode: UpdateModeForceRecovery,
			wantError:  true,
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			// Generate update package based on test inputs.
			dirPath, err := ioutil.TempDir("", "update")
			if err != nil {
				t.Fatalf("Unable to create tempdir for update package: %v", err)
			}
			defer os.RemoveAll(dirPath)
			if err := ioutil.WriteFile(filepath.Join(dirPath, "update-mode"), []byte(genValidUpdateModeJson(string(tc.updateMode))), 0666); err != nil {
				t.Fatalf("Unable to write update-mode file to tempdir: %v", err)
			}
			for _, img := range tc.images {
				if err := ioutil.WriteFile(filepath.Join(dirPath, img.Name), []byte(""), 0666); err != nil {
					t.Fatalf("Unable to write image file to tempdir: %v", err)
				}
			}

			dir, err := os.Open(dirPath)
			if err != nil {
				t.Fatalf("Unable to open tempdir: %v", err)
			}
			updatePackage := UpdatePackage{dir}

			// Assert expected result.
			if err := ValidateImgs(tc.images, &updatePackage, tc.updateMode); (err == nil) == tc.wantError {
				t.Fatalf("ValidateImages() got err [%v] want err? %t", err, tc.wantError)
			}

		})
	}
}
