// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync"
)

// Licenses is an object that facilitates operations on each License object in bulk
type Licenses struct {
	licenses []*License
}

type UnlicensedFiles struct {
	files []string
}

// NewLicenses returns a Licenses object with each license pattern loaded from the .lic folder location specified in Config
func NewLicenses(root string, prohibitedLicenseTypes []string) (*Licenses, *UnlicensedFiles, error) {
	licenses := Licenses{}
	if err := licenses.Init(root, prohibitedLicenseTypes); err != nil {
		return nil, nil, fmt.Errorf("error initializing licenses: %w", err)
	}
	unlicensedFiles := UnlicensedFiles{}
	return &licenses, &unlicensedFiles, nil
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

func (licenses *Licenses) isLicenseAValidType(prohibitedLicenseTypes []string, category string) bool {
	for _, prohibitedLicenseType := range prohibitedLicenseTypes {
		if strings.Contains(category, prohibitedLicenseType) {
			return false
		}
	}
	return true
}

// Init loads all Licenses specified in the .lic directory as defined in Config
func (licenses *Licenses) Init(root string, prohibitedLicenseTypes []string) error {
	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}
		bytes, err := LicenseWorker(path)
		if err != nil {
			return err
		}
		regex := string(bytes)
		// Skip updating white spaces, newlines, etc for files that end
		// in full.lic since they are larger.
		if !strings.HasSuffix(info.Name(), "full.lic") {
			// Update regex to ignore multiple white spaces, newlines, comments.
			regex = strings.ReplaceAll(regex, "\n", `[\s\\#\*\/]*`)
			regex = strings.ReplaceAll(regex, " ", `[\s\\#\*\/]*`)
		}
		licenses.add(&License{
			pattern:      regexp.MustCompile(regex),
			category:     info.Name(),
			validType:    licenses.isLicenseAValidType(prohibitedLicenseTypes, info.Name()),
			matches:      make(map[string]*Match),
			matchChannel: make(chan *Match, 10),
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

func (licenses *Licenses) GetFilesWithProhibitedLicenses() []string {
	var filesWithProhibitedLicenses []string
	for _, license := range licenses.licenses {
		if license.validType {
			continue
		}
		for _, match := range license.matches {
			filesWithProhibitedLicenses = append(filesWithProhibitedLicenses, match.files...)
		}
	}
	return filesWithProhibitedLicenses
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
			// TODO(jcecil): Is this an error?
			log.Printf("single license file: No result found for key %d\n", i)
			continue
		}
		if matched := result.([]byte); matched != nil {
			metrics.increment("num_single_license_file_match")
			path := strings.TrimSpace(file_tree.getPath() + base)
			licenses.MatchAuthors(string(matched), data, path, license)
			file_tree.Lock()
			file_tree.singleLicenseFiles[base] = append(file_tree.singleLicenseFiles[base], license)
			file_tree.Unlock()
		}
	}
}

func (licenses *Licenses) MatchAuthors(matched string, data []byte, path string, lic *License) {
	set := getAuthorMatches(data)
	output := make([]string, 0, len(set))
	for key := range set {
		output = append(output, key)
	}
	// Sort the authors alphabetically and join them as one string.
	sort.Strings(output)
	authors := strings.Join(output, ", ")
	// Replace < and > so that it doesn't cause special character highlights.
	authors = strings.ReplaceAll(authors, "<", "&lt")
	authors = strings.ReplaceAll(authors, ">", "&gt")

	newMatch := &Match{
		authors: authors,
		value:   matched,
		files:   []string{path},
	}
	lic.AddMatch(newMatch)
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
			log.Printf("No result found for key %d\n", i)
			continue
		}
		if matched := result.([]byte); matched != nil {
			is_matched = true
			metrics.increment("num_licensed")
			licenses.MatchAuthors(string(matched), data, path, license)
		}
	}
	return is_matched
}
