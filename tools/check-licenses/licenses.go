// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
)

// Licenses is an object that facilitates operations on each License object in bulk
type Licenses struct {
	licenses []*License
}

// NewLicenses returns a Licenses object with each license pattern loaded from the .lic folder location specified in Config
func NewLicenses(root string) (*Licenses, error) {
	licenses := Licenses{}
	if err := licenses.Init(root); err != nil {
		fmt.Println("error initializing licenses")
		return nil, err
	}
	return &licenses, nil
}

// LicenseWorker reads an aassociated .lic file into memory to be used as a License
func LicenseWorker(path string) ([]byte, error) {
	licensePatternFile, err := os.Open(path)
	defer licensePatternFile.Close()
	if err != nil {
		return nil, err
	}
	bytes, err := ioutil.ReadAll(licensePatternFile)
	if err != nil {
		return nil, err
	}
	return bytes, err
}

// Init loads all Licenses specified in the .lic directory as defined in Config
func (licenses *Licenses) Init(root string) error {
	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if info.IsDir() {
			return nil
		}
		bytes, err := LicenseWorker(path)
		if err != nil {
			return err
		}
		str := string(bytes)
		// Update regex to ignore multiple white spaces, newlines, comments.
		updatedRegex := strings.ReplaceAll(str, "\n", `[\s\\#\*]*`)
		updatedRegex = strings.ReplaceAll(updatedRegex, " ", `[\s\\#\*]*`)
		licenses.add(&License{
			pattern:  regexp.MustCompile(updatedRegex),
			category: info.Name(),
		})
		return nil
	})
	if err != nil {
		return err
	}
	if len(licenses.licenses) == 0 {
		return errors.New("no licenses")
	}
	return nil
}

func (licenses *Licenses) add(license *License) {
	licenses.licenses = append(licenses.licenses, license)
}

func (licenses *Licenses) MatchSingleLicenseFile(data []byte, base string, metrics *Metrics, file_tree *FileTree) {
	// TODO(solomonokinard) deduplicate Match*File()
	var wg sync.WaitGroup
	wg.Add(len(licenses.licenses))
	var sm sync.Map
	for i, license := range licenses.licenses {
		go license.LicenseFindMatch(i, data, &sm, &wg)
	}
	wg.Wait()
	for i, license := range licenses.licenses {
		result, found := sm.Load(i)
		if !found {
			fmt.Printf("single license file: No result found for key %d\n", i)
			continue
		}
		if matched := result.([]byte); matched != nil {
			metrics.increment("num_single_license_file_match")
			file_tree.singleLicenseFiles[base] = append(file_tree.singleLicenseFiles[base], license)
		}
	}
}

// MatchFile returns true if any License matches input data
func (licenses *Licenses) MatchFile(data []byte, path string, metrics *Metrics) bool {
	is_matched := false
	var wg sync.WaitGroup
	wg.Add(len(licenses.licenses))
	var sm sync.Map
	for i, license := range licenses.licenses {
		go license.LicenseFindMatch(i, data, &sm, &wg)
	}
	wg.Wait()
	for i, license := range licenses.licenses {
		result, found := sm.Load(i)
		if !found {
			fmt.Printf("No result found for key %d\n", i)
			continue
		}
		if matched := result.([]byte); matched != nil {
			is_matched = true
			metrics.increment("num_licensed")
			if len(license.matches) == 0 {
				license.matches = append(license.matches, Match{
					value: matched})
			}
			license.matches[0].files = append(license.matches[0].files, path)
		}
	}
	return is_matched
}
