// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"runtime/trace"
	"sort"
	"strings"
)

// Licenses is an object that facilitates operations on each License object in bulk
type Licenses struct {
	licenses []*License
}

// NewLicenses returns a Licenses object with each license pattern loaded from
// the .lic folder location specified in Config
func NewLicenses(ctx context.Context, config *Config) (*Licenses, error) {
	defer trace.StartRegion(ctx, "NewLicenses").End()
	l := &Licenses{}
	err := filepath.Walk(config.LicensePatternDir,
		func(path string, info os.FileInfo, err error) error {
			if info.IsDir() {
				return nil
			}
			license, err := NewLicense(path, config)
			if err != nil {
				return err
			}
			l.licenses = append(l.licenses, license)
			return nil
		})
	if err != nil {
		return nil, err
	}

	if len(l.licenses) == 0 {
		return nil, errors.New("no licenses")
	}

	sort.Sort(licenseByPattern(l.licenses))

	return l, nil
}

func (l *Licenses) GetFilesWithProhibitedLicenses() []string {
	var filesWithProhibitedLicenses []string
	set := map[string]bool{}
	for _, license := range l.licenses {
		if license.ValidType {
			continue
		}
		for _, match := range license.matches {
			for _, path := range match.files {
				if _, found := set[path]; !found && !CheckLicenseAllowList(license, path) {
					set[path] = true
					filesWithProhibitedLicenses = append(filesWithProhibitedLicenses, path)
				}
			}
		}
	}
	return filesWithProhibitedLicenses
}

func (l *Licenses) GetFilesWithBadLicenseUsage() []string {
	var filesWithBadLicenseUsage []string
	set := map[string]bool{}
	for _, license := range l.licenses {
		if len(license.BadLicenseUsage) > 0 {
			for _, path := range license.BadLicenseUsage {
				if _, found := set[path]; !found {
					set[path] = true
					filesWithBadLicenseUsage = append(filesWithBadLicenseUsage, path)
				}
			}
		}
	}
	return filesWithBadLicenseUsage
}

func CheckLicenseAllowList(license *License, path string) bool {
	var licenseAllowed = true
	if len(license.AllowedDirs) > 0 && !contains(license.AllowedDirs, path) {
		license.BadLicenseUsage = append(license.BadLicenseUsage, path)
		licenseAllowed = false
	}
	return licenseAllowed
}

func (l *Licenses) MatchSingleLicenseFile(data []byte, path string, metrics *Metrics, ft *FileTree) {
	for _, license := range l.licenses {
		if license.Search(data, path) && CheckLicenseAllowList(license, path) {
			metrics.increment("num_single_license_file_match")
			ft.Lock()
			ft.SingleLicenseFiles[path] = append(ft.SingleLicenseFiles[path], license)
			ft.Unlock()
		}
	}
}

// MatchFile returns true if any License matches input data
// along with the license that matched. It returns false and nil
// if there were no matches.
func (l *Licenses) MatchFile(data []byte, path string, metrics *Metrics) (bool, *License) {
	for _, license := range l.licenses {
		if license.Search(data, path) && CheckLicenseAllowList(license, path) {
			metrics.increment("num_licensed")
			return true, license
		}
	}
	return false, nil
}

func contains(matches []string, item string) bool {
	for _, m := range matches {
		if strings.Contains(item, m) {
			return true
		}
	}
	return false
}
