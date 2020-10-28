// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"runtime/trace"
	"strings"
	"sync"

	"golang.org/x/sync/errgroup"
)

type UnlicensedFiles struct {
	files []string
}

// Walk gathers all Licenses then checks for a match within each filtered file
func Walk(ctx context.Context, config *Config) error {
	var eg errgroup.Group
	var wg sync.WaitGroup
	metrics := new(Metrics)
	metrics.Init()
	file_tree := NewFileTree(ctx, config, metrics)
	licenses, err := NewLicenses(ctx, config.LicensePatternDir, config.ProhibitedLicenseTypes)
	if err != nil {
		return err
	}
	unlicensedFiles := &UnlicensedFiles{}
	for _, l := range licenses.licenses {
		l := l
		wg.Add(1)
		go func() {
			l.MatchChannelWorker()
			wg.Done()
		}()
	}

	r := trace.StartRegion(ctx, "singleLicenseFile walk")
	for tree := range file_tree.getSingleLicenseFileIterator() {
		tree := tree
		for singleLicenseFile := range tree.SingleLicenseFiles {
			singleLicenseFile := singleLicenseFile
			eg.Go(func() error {
				if err := processSingleLicenseFile(singleLicenseFile, metrics, licenses, config, tree); err != nil {
					// error safe to ignore because eg. io.EOF means symlink hasn't been handled yet
					// TODO(jcecil): Correctly skip symlink.
					fmt.Printf("warning: %s. Skipping file: %s.\n", err, tree.getPath())
				}
				return nil
			})
		}
	}
	eg.Wait()
	r.End()

	r = trace.StartRegion(ctx, "regular file walk")
	for path := range file_tree.getFileIterator() {
		path := path
		eg.Go(func() error {
			if err := processFile(path, metrics, licenses, unlicensedFiles, config, file_tree); err != nil {
				// TODO(jcecil): Correctly skip symlink and return errors.
				fmt.Printf("warning: %s. Skipping file: %s.\n", err, path)
			}
			return nil
		})
	}
	eg.Wait()

	// Close each licenses's matchChannel goroutine by sending a nil object.
	for _, l := range licenses.licenses {
		l.AddMatch(nil)
	}
	wg.Wait()
	r.End()

	defer trace.StartRegion(ctx, "finalization").End()

	if config.OutputTreeStateFilename != "" {
		file_tree.saveTreeState(config.OutputTreeStateFilename)
	}

	if config.ExitOnProhibitedLicenseTypes {
		filesWithProhibitedLicenses := licenses.GetFilesWithProhibitedLicenses()
		if len(filesWithProhibitedLicenses) > 0 {
			files := strings.Join(filesWithProhibitedLicenses, "\n")
			return fmt.Errorf("Encountered prohibited license types. File paths are:\n%v", files)
		}
	}

	if config.ExitOnUnlicensedFiles && len(unlicensedFiles.files) > 0 {
		files := strings.Join(unlicensedFiles.files, "\n")
		return fmt.Errorf("Encountered files that are missing licenses. File paths are:\n%v", files)
	}

	if config.OutputLicenseFile {
		path := config.OutputFilePrefix + "." + config.OutputFileExtension
		if err := saveToOutputFile(path, licenses); err != nil {
			return err
		}
	}
	metrics.print()
	return nil
}

func processSingleLicenseFile(base string, metrics *Metrics, licenses *Licenses, config *Config, file_tree *FileTree) error {
	path := strings.TrimSpace(file_tree.getPath() + base)
	// For singe license files, we read the whole file.
	data, err := ioutil.ReadFile(path)
	if err != nil {
		return err
	}
	licenses.MatchSingleLicenseFile(data, base, metrics, file_tree)
	return nil
}

func processFile(path string, metrics *Metrics, licenses *Licenses, unlicensedFiles *UnlicensedFiles, config *Config, file_tree *FileTree) error {
	log.Printf("visited file or dir: %q", path)
	// TODO(omerlevran): Reuse the buffer.
	data := make([]byte, config.MaxReadSize)
	n, err := readFile(path, data)
	if err != nil {
		return err
	}
	data = data[:n]
	is_matched := licenses.MatchFile(data, path, metrics)
	if !is_matched {
		project := file_tree.getProjectLicense(path)
		if project == nil {
			metrics.increment("num_unlicensed")
			unlicensedFiles.files = append(unlicensedFiles.files, path)
			fmt.Printf("File license: missing. Project license: missing. path: %s\n", path)
		} else {
			metrics.increment("num_with_project_license")
			for _, arr_license := range project.SingleLicenseFiles {

				for i, license := range arr_license {
					license.mu.Lock()
					for author := range license.matches {
						license.matches[author].files = append(license.matches[author].files, path)
					}
					license.mu.Unlock()
					if i == 0 {
						metrics.increment("num_one_file_matched_to_one_single_license")
					}
					log.Printf("project license: %s", license.category)
					metrics.increment("num_one_file_matched_to_multiple_single_licenses")
				}
			}
			log.Printf("File license: missing. Project license: exists. path: %s", path)
		}
	}
	return nil
}

// readFile returns up to n bytes from the file.
func readFile(path string, d []byte) (int, error) {
	f, err := os.Open(path)
	if err != nil {
		return 0, err
	}
	defer f.Close()
	n, err := f.Read(d)
	if err == io.EOF {
		err = nil
	}
	return n, err
}
