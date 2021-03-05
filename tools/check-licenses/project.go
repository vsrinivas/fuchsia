// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

type Project struct {
	Name               string
	Root               string
	LicenseFiles       []string
	LicenseFileMatches []*Match
}

func NewProjectFromReadme() *Project {
	return &Project{}
}

func NewProjectFromCustomEntry() *Project {
	return &Project{}
}

func NewProjectFromLicenseFile() *Project {
	return &Project{}
}
