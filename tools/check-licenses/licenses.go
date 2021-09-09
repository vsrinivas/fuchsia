// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime/trace"
	"sort"
	"sync"
)

// Licenses is an object that facilitates operations on each License object in bulk.
type Licenses struct {
	licenses []*License
	notices  []*License

	sync.RWMutex
}

// NewLicenses returns a Licenses object with each license pattern loaded from
// the .lic folder location specified in the config file.
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
			if !match.Used {
				continue
			}
			for _, path := range match.Files {
				if !contains(license.AllowedDirs, path) {
					fmt.Fprintf(os.Stderr, "Prohibited: %q in %q\n", license.Category, path)
					if _, found := set[path]; !found {
						set[path] = true
						filesWithProhibitedLicenses = append(filesWithProhibitedLicenses, path)
					}
				}
			}
		}
	}

	sort.Strings(filesWithProhibitedLicenses)
	return filesWithProhibitedLicenses
}

func (l *Licenses) GetFilesWithBadLicenseUsage() []string {
	var filesWithBadLicenseUsage []string
	set := map[string]bool{}
	for _, license := range l.licenses {
		if len(license.BadLicenseUsage) > 0 {
			for _, path := range license.BadLicenseUsage {
				fmt.Fprintf(os.Stderr, "Not allowlisted: %q in %q\n", license.Category, path)
				if _, found := set[path]; !found {
					set[path] = true
					filesWithBadLicenseUsage = append(filesWithBadLicenseUsage, path)
				}
			}
		}
	}

	sort.Strings(filesWithBadLicenseUsage)
	return filesWithBadLicenseUsage
}

func (l *Licenses) MatchSingleLicenseFile(data []byte, path string, metrics *Metrics, ft *FileTree) {
	for _, license := range l.licenses {
		if ok, match := license.Search(data, path); ok {

			// Mark all single licenses file matches as used. Some of these files are used in the project
			// directories that are skipped when traversing the file tree, so the files use the license
			// won't be processed and marked as Used. This means that these files will be included in the
			// output when analysing a GN target even if there is no dependency on the license but it is
			// better to have false positives and not false negatives.
			match.Lock()
			match.Used = true
			match.Unlock()

			metrics.increment("num_single_license_file_match")
			ft.Lock()
			ft.SingleLicenseFiles[path] = append(ft.SingleLicenseFiles[path], license)
			ft.LicenseMatches[path] = append(ft.LicenseMatches[path], match)
			ft.Unlock()
		}
	}
}

func (l *Licenses) MatchNoticeFile(data []byte, path string, metrics *Metrics, ft *FileTree) {
	custom := NewCustomLicense(path)
	l.Lock()
	l.notices = append(l.notices, custom)
	l.Unlock()

	if ok, match := custom.Search(data, path); ok {
		metrics.increment("num_single_license_file_match")
		ft.Lock()
		ft.SingleLicenseFiles[path] = append(ft.SingleLicenseFiles[path], custom)
		ft.LicenseMatches[path] = append(ft.LicenseMatches[path], match)
		ft.Unlock()
	} else {
		fmt.Printf("Error: failed to match custom license text '%v'\n", path)
	}
}

// MatchFile returns true if any License matches input data
// along with the license that matched. It returns false and nil
// if there were no matches.
func (l *Licenses) MatchFile(data []byte, path string, metrics *Metrics) (bool, *License, *Match) {
	for _, license := range l.licenses {
		if ok, match := license.Search(data, path); ok {
			metrics.increment("num_licensed")
			return true, license, match
		}
	}
	return false, nil, nil
}
