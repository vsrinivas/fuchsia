// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"runtime/pprof"
	"runtime/trace"
	"strings"

	checklicenses "go.fuchsia.dev/fuchsia/tools/check-licenses"
)

var (
	configFile = flag.String("config_file", "tools/check-licenses/config/config.json", "Comma separated list of paths to json files.")
	pproffile  = flag.String("pprof", "", "generate file that can be parsed by go tool pprof")
	tracefile  = flag.String("trace", "", "generate file that can be parsed by go tool trace")

	skipDirs  = flag.String("skip_dirs", "", "Comma separated list of directory names to skip when traversing a directory tree. This arg is added to the list of skipdirs in the config file.")
	skipFiles = flag.String("skip_files", "", "Comma separated list of file names to skip when traversing a directory tree. This arg is added to the list of skipfiles in the config file.")

	prohibitedLicenseTypes       = flag.String("prohibited_license_types", "", "Comma separated list of license types that are prohibited. This arg is added to the list of prohibitedLicenseTypes in the config file.")
	exitOnDirRestrictedLicense   = flag.Bool("exit_on_dir_restricted_license", true, "If true, exits if it encounters a license used outside of its allowed directories.")
	exitOnProhibitedLicenseTypes = flag.Bool("exit_on_prohibited_license_types", true, "If true, exits if it encounters a prohibited license type.")
	exitOnUnlicensedFiles        = flag.Bool("exit_on_unlicensed_files", true, "If true, exits if it encounters files that are unlicensed.")

	licensePatternDir = flag.String("license_pattern_dir", "", "Location of directory containing pattern files for all licenses that may exist in the given code base.")
	noticeTxtFiles    = flag.String("notice_txt", "", "Comma separated list of NOTICE.txt files to parse, in addition to those listed in config.json.")
	licenseAllowList  = flag.String("license_allow_list", "", "Map of license pattern file (.lic) to list of directories where the license can be used.")

	logLevel          = flag.Int("log_level", 0, "Log level")
	baseDir           = flag.String("base_dir", "", "Root location to begin directory traversal.")
	outDir            = flag.String("out_dir", "", "Directory to write outputs to.")
	outputLicenseFile = flag.Bool("output_license_file", true, "If true, outputs a license file with all the licenses for the project.")
	target            = flag.String("target", "", "Analyze the dependency tree of a specific GN build target.")
	buildDir          = flag.String("build_dir", os.Getenv("FUCHSIA_BUILD_DIR"), "Location of GN build directory.")
	gnPath            = flag.String("gn_path", "", "Path to GN executable. Required when target is specified.")
)

func mainImpl() error {
	flag.Parse()
	if *tracefile != "" {
		f, err := os.Create(*tracefile)
		if err != nil {
			return fmt.Errorf("failed to create trace output file: %s", err)
		}
		defer f.Close()
		if err := trace.Start(f); err != nil {
			return fmt.Errorf("failed to start trace: %s", err)
		}
		defer trace.Stop()
	}
	if *pproffile != "" {
		f, err := os.Create(*pproffile)
		if err != nil {
			return fmt.Errorf("failed to create pprof output file: %s", err)
		}
		defer f.Close()
		if err := pprof.StartCPUProfile(f); err != nil {
			return fmt.Errorf("failed to start pprof: %s", err)
		}
		defer pprof.StopCPUProfile()
	}

	// TODO(jcecil): incorporate https://godoc.org/github.com/golang/glog
	if *logLevel == 0 {
		log.SetOutput(ioutil.Discard)
	}

	config := &checklicenses.Config{}

	if *configFile != "" {
		split := strings.Split(*configFile, ",")
		for _, path := range split {
			if path != "" {
				c, err := checklicenses.NewConfig(path)
				if err != nil {
					return fmt.Errorf("failed to initialize config %s: %s", path, err)
				}
				config.Merge(c)
			}
		}
	}

	if *skipDirs != "" {
		split := strings.Split(*skipDirs, ",")
		for _, s := range split {
			if s != "" {
				config.SkipDirs = append(config.SkipDirs, s)
			}
		}
	}

	config.SkipDirs = append(config.SkipDirs, additionalSkipDirs...)

	if *skipFiles != "" {
		split := strings.Split(*skipFiles, ",")
		for _, s := range split {
			if s != "" {
				config.SkipFiles = append(config.SkipFiles, s)
			}
		}
	}

	// TODO(b/172070492): Remove this list once completed.
	for _, f := range additionalSkipFiles {
		config.SkipFiles = append(config.SkipFiles, strings.ToLower(f))
	}

	if *prohibitedLicenseTypes != "" {
		split := strings.Split(*prohibitedLicenseTypes, ",")
		for _, s := range split {
			if s != "" {
				config.ProhibitedLicenseTypes = append(config.ProhibitedLicenseTypes, s)
			}
		}
	}

	if *noticeTxtFiles != "" {
		split := strings.Split(*noticeTxtFiles, ",")
		for _, s := range split {
			if s != "" {
				config.NoticeTxtFiles = append(config.NoticeTxtFiles, s)
			}
		}
	}

	if *licenseAllowList != "" {
		blob := []byte(*licenseAllowList)
		result := make(map[string][]string)
		err := json.Unmarshal(blob, &result)
		if err != nil {
			return fmt.Errorf("failed to initialize license allow list: %s", err)
		}

		for k, v := range result {
			if _, ok := config.LicenseAllowList[k]; !ok {
				config.LicenseAllowList[k] = []string{}
			}
			config.LicenseAllowList[k] = append(config.LicenseAllowList[k], v...)
		}
	}

	// TODO(fxb/42986): Remove ExitOnProhibitedLicenseTypes and ExitOnUnlicensedFiles
	// flags once fxb/42986 is completed.
	config.ExitOnProhibitedLicenseTypes = *exitOnProhibitedLicenseTypes
	config.ExitOnUnlicensedFiles = *exitOnUnlicensedFiles
	config.ExitOnDirRestrictedLicense = *exitOnDirRestrictedLicense

	config.OutputLicenseFile = *outputLicenseFile

	if *licensePatternDir != "" {
		if info, err := os.Stat(*licensePatternDir); os.IsNotExist(err) && info.IsDir() {
			return fmt.Errorf("license pattern directory path %q does not exist!", *licensePatternDir)
		}
		config.LicensePatternDir = *licensePatternDir
	}

	if *baseDir != "" {
		info, err := os.Stat(*baseDir)
		if os.IsNotExist(err) {
			return fmt.Errorf("base directory path %q does not exist!", *baseDir)
		}
		if err != nil {
			return err
		}
		if !info.IsDir() {
			return fmt.Errorf("base directory path %q is not a directory!", *baseDir)
		}
		config.BaseDir = *baseDir
	}

	if *outDir != "" {
		info, err := os.Stat(*outDir)
		if os.IsNotExist(err) {
			return fmt.Errorf("out directory path %q does not exist!", *outDir)
		}
		if err != nil {
			return err
		}
		if !info.IsDir() {
			return fmt.Errorf("out directory path %q is not a directory!", *outDir)
		}
		config.OutDir = *outDir
	}

	config.BuildDir = *buildDir
	if *gnPath != "" {
		_, err := os.Stat(*gnPath)
		if os.IsNotExist(err) {
			return fmt.Errorf("GN path %q does not exist!", *gnPath)
		}
		if err != nil {
			return err
		}
		config.GnPath = *gnPath
	} else {
		prebuilt := os.Getenv("PREBUILT_3P_DIR")
		hostPlatform := os.Getenv("HOST_PLATFORM")
		if prebuilt != "" && hostPlatform != "" {
			config.GnPath = filepath.Join(prebuilt, "gn", hostPlatform, "gn")
		}
	}

	if *target != "" {
		if config.GnPath == "" {
			return fmt.Errorf("A target was specified but no path to GN was given")
		}
		config.Target = *target
	}

	if err := checklicenses.Run(context.Background(), config); err != nil {
		return fmt.Errorf("failed to analyze the given directory: %v", err)
	}
	return nil
}

func main() {
	if err := mainImpl(); err != nil {
		fmt.Fprintf(os.Stderr, "check-licenses: %s\nSee go/fuchsia-licenses-playbook for information on resolving common errors.\n", err)
		os.Exit(1)
	}
}
