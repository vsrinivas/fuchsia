// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

import (
	"crypto/sha1"
	"encoding/base64"
	"fmt"
	"io/ioutil"
	"path/filepath"
)

type FileType string

const (
	Any                  FileType = "any"
	CopyrightHeader               = "copyright_header"
	SingleLicense                 = "single_license"
	MultiLicenseChromium          = "multi_license_chromium"
	MultiLicenseFlutter           = "multi_license_flutter"
	MultiLicenseGoogle            = "multi_license_google"
)

var FileTypes map[string]FileType

func init() {
	FileTypes = map[string]FileType{
		"any":                    Any,
		"copyright_header":       CopyrightHeader,
		"single_license":         SingleLicense,
		"multi_license_chromium": MultiLicenseChromium,
		"multi_license_flutter":  MultiLicenseFlutter,
		"multi_license_google":   MultiLicenseGoogle,
	}
}

type File struct {
	Name string
	Path string `json:"path"`

	LicenseFormat FileType `json:"licenseFormat"`
}

func NewFile(path string) *File {
	return &File{
		Name:          filepath.Base(path),
		Path:          path,
		LicenseFormat: Any,
	}
}

// There are many duplicate LICENSE and NOTICE files in the fuchsia repository.
// We can save a lot of search time by detecting duplicates and reusing results.
func (f *File) Hash() (string, error) {
	data, err := f.ReadAll()
	if err != nil {
		return "", err
	}

	hasher := sha1.New()
	hasher.Write(data)
	return base64.URLEncoding.EncodeToString(hasher.Sum(nil)), nil
}

func (f *File) ReadAll() ([]byte, error) {
	// TODO: Handle ascii / utf-8 conversion here.
	return ioutil.ReadFile(f.Path)
}

func (f *File) SetLicenseFormat(format string) error {
	if val, ok := FileTypes[format]; ok {
		f.LicenseFormat = val
		return nil
	}
	return fmt.Errorf("Format %v isn't a valid License Format.", format)
}
