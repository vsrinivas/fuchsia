// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

import (
	"bytes"
	"crypto/sha1"
	"encoding/base64"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file/notice"
)

// FileData holds the text information (and some metadata) for a given file.
//
// Many NOTICE files will include several license texts in it.
// FileData represents one of those segments. It also maintains a line number
// that points to the location of this license text in the original NOTICE file,
// making it easier to find this license text again later.
type FileData struct {
	FilePath    string
	LibraryName string
	LineNumber  int
	Data        []byte
	LicenseType string
	URL         string

	hash string
}

// Order implements sort.Interface for []*FileData based on the FilePath field.
type OrderFileData []*FileData

func (a OrderFileData) Len() int      { return len(a) }
func (a OrderFileData) Swap(i, j int) { a[i], a[j] = a[j], a[i] }
func (a OrderFileData) Less(i, j int) bool {
	if a[i].FilePath < a[j].FilePath {
		return true
	}
	if a[i].FilePath > a[j].FilePath {
		return false
	}
	return a[i].LineNumber < a[j].LineNumber
}

func NewFileData(path string, content []byte, filetype FileType) ([]*FileData, error) {
	data := make([]*FileData, 0)

	// The "LicenseFormat" field of each file is set at the project level
	// (in README.fuchsia files) and it affects how they are analyzed here.
	switch filetype {

	// Default: File.LicenseFormat == Any
	// This is most likely a regular source file in the repository.
	// May or may not have copyright information.
	case Any:
		data = append(data, &FileData{
			FilePath:   path,
			LineNumber: 0,
			Data:       bytes.TrimSpace(content),
		})

	// File.LicenseFormat == CopyrightHeader
	// All source files belonging to "The Fuchsia Authors" (fuchsia.git)
	// must contain Copyright header information.
	case CopyrightHeader:
		data = append(data, &FileData{
			FilePath:   path,
			LineNumber: 0,
			Data:       bytes.TrimSpace(content),
		})

	// File.LicenseFormat == SingleLicense
	// Regular LICENSE files that contain text for a single license.
	case SingleLicense:
		data = append(data, &FileData{
			FilePath:   path,
			LineNumber: 0,
			Data:       bytes.TrimSpace(content),
		})

	// File.LicenseFormat == MultiLicense*
	// NOTICE files that contain text for multiple licenses.
	// See the files in the /notice subdirectory for more info.
	case MultiLicenseChromium:
		ndata, err := notice.ParseChromium(path, content)
		if err != nil {
			return nil, err
		}
		for _, d := range ndata {
			data = append(data, &FileData{
				FilePath:    path,
				LineNumber:  d.LineNumber,
				LibraryName: d.LibraryName,
				Data:        bytes.TrimSpace(d.LicenseText),
			})
		}
	case MultiLicenseFlutter:
		ndata, err := notice.ParseFlutter(path, content)
		if err != nil {
			return nil, err
		}
		for _, d := range ndata {
			data = append(data, &FileData{
				FilePath:    path,
				LineNumber:  d.LineNumber,
				LibraryName: d.LibraryName,
				Data:        bytes.TrimSpace(d.LicenseText),
			})
		}
	case MultiLicenseAndroid:
		ndata, err := notice.ParseAndroid(path, content)
		if err != nil {
			return nil, err
		}
		for _, d := range ndata {
			data = append(data, &FileData{
				FilePath:    path,
				LineNumber:  d.LineNumber,
				LibraryName: d.LibraryName,
				Data:        bytes.TrimSpace(d.LicenseText),
			})
		}
	case MultiLicenseGoogle:
		ndata, err := notice.ParseGoogle(path, content)
		if err != nil {
			return nil, err
		}
		for _, d := range ndata {
			data = append(data, &FileData{
				FilePath:    path,
				LineNumber:  d.LineNumber,
				LibraryName: d.LibraryName,
				Data:        bytes.TrimSpace(d.LicenseText),
			})
		}
	}

	for _, d := range data {
		for _, r := range Config.Replacements {
			d.Data = bytes.ReplaceAll(d.Data, []byte(r.Replace), []byte(r.With))
		}
	}
	return data, nil
}

func (fd *FileData) SetData(data []byte) {
	fd.Data = data
	fd.hash = ""
	fd.Hash()
}

// Hash the content of this filedata object, to help detect duplicate texts
// and help reduce the final NOTICE filesize.
func (fd *FileData) Hash() string {
	if len(fd.hash) > 0 {
		return fd.hash
	}

	hasher := sha1.New()
	hasher.Write(bytes.TrimSpace(fd.Data))
	fd.hash = base64.URLEncoding.EncodeToString(hasher.Sum(nil))
	return fd.hash
}
