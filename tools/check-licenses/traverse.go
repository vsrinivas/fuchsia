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
	"path/filepath"
	"runtime"
	"runtime/trace"
	"sort"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/noticetxt"
	"golang.org/x/sync/errgroup"
)

type UnlicensedFiles struct {
	files []string
}

const exampleHeader = "# Copyright %d The Fuchsia Authors. All rights reserved.\n# Use of this source code is governed by a BSD-style license that can be\n# found in the LICENSE file."

// Run executes the license verification according to the provided config file.
func Run(ctx context.Context, config *Config) error {
	var eg errgroup.Group
	var err error
	// Blocked goroutines do not count against GOMAXPROCS. Limit
	// ourselves to at most doubling the thread count.
	var workSem = make(chan int, runtime.GOMAXPROCS(0))
	metrics := NewMetrics()

	dependencies := make([]string, 0)
	if config.Target != "" {
		gn, err := NewGn(ctx, config)
		if err != nil {
			return err
		}

		deps, err := gn.Dependencies(ctx, config.Target)
		if err != nil {
			return err
		}

		for _, dep := range deps {
			d, err := LabelToDirectory(dep)
			if err != nil {
				return err
			}
			dependencies = append(dependencies, d)
		}
	}

	file_tree, err := NewFileTree(ctx, config.BaseDir, nil, config, metrics)
	if err != nil {
		return err
	}

	licenses, err := NewLicenses(ctx, config)
	if err != nil {
		return err
	}
	unlicensedFiles := &UnlicensedFiles{}

	r := trace.StartRegion(ctx, "singleLicenseFile walk")
	for tree := range file_tree.getFileTreeIterator() {
		tree := tree

		// tree.SingleLicenseFiles may be modified while processing license files. So instead of
		// iterating on it here, which might trigger a concurrent map iteration and write error, first
		// create a list of filenames to iterate on. The map writes never add new paths so this is safe.
		tree.RLock()
		licenseFilePaths := make([]string, len(tree.SingleLicenseFiles))
		for licenseFile := range tree.SingleLicenseFiles {
			licenseFilePaths = append(licenseFilePaths, licenseFile)
		}
		tree.RUnlock()

		for _, licenseFile := range licenseFilePaths {
			licenseFile := licenseFile
			workSem <- 1
			eg.Go(func() error {
				if err := processLicenseFile(licenseFile, metrics, licenses, config, tree); err != nil {
					// error safe to ignore because eg. io.EOF means symlink hasn't been handled yet
					// TODO(jcecil): Correctly skip symlink.
					log.Printf("warning: %s. Skipping file: %s.\n", err, licenseFile)
				}
				<-workSem
				return nil
			})
		}
	}
	eg.Wait()
	r.End()

	file_tree.propagateProjectLicenses(config)

	r = trace.StartRegion(ctx, "processFlutterLicenses")
	if err = processFlutterLicenses(licenses, config, metrics, file_tree); err != nil {
		log.Printf("error processing flutter licenses: %v", err)
	}
	r.End()

	r = trace.StartRegion(ctx, "processChromiumLicenses")
	if err = processChromiumLicenses(licenses, config, metrics, file_tree); err != nil {
		log.Printf("error processing chromium licenses: %v", err)
	}
	r.End()

	r = trace.StartRegion(ctx, "processNoticeTxtFiles")
	if err = processNoticeTxtFiles(licenses, config, metrics, file_tree); err != nil {
		log.Printf("error processing NOTICE.txt files: %v", err)
	}
	r.End()

	r = trace.StartRegion(ctx, "regular file walk")
	for file := range file_tree.getFileIterator() {
		file := file

		// Skip files that aren't dependencies of the specified GN target, if there is one.
		skip := true
		if len(dependencies) == 0 {
			skip = false
		}
		for _, dep := range dependencies {
			if file.Parent.GnTarget == dep {
				skip = false
				break
			}
		}
		if skip {
			continue
		}

		workSem <- 1
		eg.Go(func() error {
			if err := processFile(file, metrics, licenses, unlicensedFiles, config); err != nil {
				// TODO(jcecil): Correctly skip symlink and return errors.
				log.Printf("warning: %s. Skipping file: %s.\n", err, file.Path)
			}
			<-workSem
			return nil
		})
	}
	eg.Wait()
	r.End()

	defer trace.StartRegion(ctx, "finalization").End()

	if config.ExitOnProhibitedLicenseTypes {
		filesWithProhibitedLicenses := licenses.GetFilesWithProhibitedLicenses()
		if len(filesWithProhibitedLicenses) > 0 {
			return fmt.Errorf("Encountered prohibited license types.\nPlease remove the offending files, or reach out to //tools/check-licenses/OWNERS for license exceptions or errors.")
		}
	}
	year, _, _ := time.Now().Date()
	header := fmt.Sprintf(exampleHeader, year)

	if config.ExitOnUnlicensedFiles && len(unlicensedFiles.files) > 0 {
		sort.Strings(unlicensedFiles.files)
		files := strings.Join(unlicensedFiles.files, "\n")
		return fmt.Errorf("Encountered files with licenses that are either malformed or missing. File paths are:\n\n%v\n\nPlease add license information to the headers of each file. If this is Fuchsia code (e.g. not in //prebuilt, //third_party, etc), paste this example header text into the top of each file (replacing '#' with the proper comment character for your file):\n\n%s\n\nReach out to //tools/check-licenses/OWNERS for file exceptions or errors.\n", files, header)
	}

	if config.ExitOnDirRestrictedLicense {
		filesWithBadLicenseUsage := licenses.GetFilesWithBadLicenseUsage()
		if len(filesWithBadLicenseUsage) > 0 {
			return fmt.Errorf("Encountered files with licenses that may not be used in those directories.\nPlease remove the offending files, or reach out to //tools/check-licenses/OWNERS for license exceptions or errors.")
		}
	}

	if config.OutputLicenseFile {
		for _, extension := range config.OutputFileExtensions {
			path := config.OutputFilePrefix + "." + extension
			if err := saveToOutputFile(filepath.Join(config.OutDir, path), licenses, config); err != nil {
				return err
			}
		}
	}
	log.Print(metrics.string())
	return nil
}

func processLicenseFile(path string, metrics *Metrics, licenses *Licenses, config *Config, file_tree *FileTree) error {
	// For license files, we read the whole file.
	// TODO: "traverse.go" shouldn't have to worry about file access.
	// Read file data in file.go, and pass around the file object everywhere.
	data, err := ioutil.ReadFile(path)
	if err != nil {
		return err
	}
	file, _ := NewFile(path, file_tree)
	if contains(config.NoticeFiles, path) {
		licenses.MatchNoticeFile(data, file.Path, metrics, file_tree)
	} else {
		licenses.MatchSingleLicenseFile(data, file.Path, metrics, file_tree)
	}
	return nil
}

func processFile(file *File, metrics *Metrics, licenses *Licenses, unlicensedFiles *UnlicensedFiles, config *Config) error {
	file_tree := file.Parent

	path := file.Path
	log.Printf("visited file or dir: %q", path)
	// TODO(omerlevran): Reuse the buffer.
	data := make([]byte, config.MaxReadSize)
	n, err := readFile(path, data)
	if err != nil {
		return err
	}
	data = data[:n]

	isMatched, matchedLic, licMatch := licenses.MatchFile(data, file.Path, metrics)
	if isMatched {
		file.Licenses = append(file.Licenses, matchedLic)
		licMatch.Lock()
		licMatch.Used = true
		licMatch.Unlock()
	} else {
		if len(file_tree.SingleLicenseFiles) == 0 {
			metrics.increment("num_unlicensed")
			unlicensedFiles.files = append(unlicensedFiles.files, path)
			log.Printf("File license: missing. Project license: missing. path: %s\n", path)
		} else {
			// If we find a LICENSE file but it doesn't match any of our license patterns,
			// we should mark it as unlicensed.
			foundMatchedLicense := false
			for _, l := range file_tree.SingleLicenseFiles {
				if len(l) > 0 {
					foundMatchedLicense = true
					break
				}
			}
			if !foundMatchedLicense {
				metrics.increment("num_unlicensed")
				unlicensedFiles.files = append(unlicensedFiles.files, path)
				log.Printf("File license: missing. Project license: missing. path: %s\n", path)
			} else {
				metrics.increment("num_with_project_license")
				for _, matches := range file_tree.LicenseMatches {
					for _, match := range matches {
						match.Lock()
						match.LicenseAppliesToFiles = append(match.LicenseAppliesToFiles, path)
						match.Used = true
						match.Unlock()
					}
				}
				log.Printf("File license: missing. Project license: exists. path: %s", path)
			}
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

func processNoticeTxtFiles(licenses *Licenses, config *Config, metrics *Metrics, file_tree *FileTree) error {
	for _, path := range config.NoticeTxtFiles {
		file, _ := NewFile(path, file_tree)
		data, err := noticetxt.ParseNoticeTxtFile(path)
		if err != nil {
			return err
		}
		for _, d := range data {
			licenses.MatchSingleLicenseFile(d, file.Path, metrics, file_tree)
		}
	}
	return nil
}
