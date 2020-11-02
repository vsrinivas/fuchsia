// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"runtime/pprof"
	"runtime/trace"
	"strings"

	checklicenses "go.fuchsia.dev/fuchsia/tools/check-licenses"
)

var config checklicenses.Config

var (
	configFile = flag.String("config_file", "tools/check-licenses/config/config.json", "Location of config.json.")

	skipDirs                     = flag.String("skip_dirs", "", "Comma separated list of directory names to skip when traversing a directory tree. This arg is added to the list of skipdirs in the config file.")
	skipFiles                    = flag.String("skip_files", "", "Comma separated list of file names to skip when traversing a directory tree. This arg is added to the list of skipfiles in the config file.")
	licensePatternDir            = flag.String("license_pattern_dir", "", "Location of directory containing pattern files for all licenses that may exist in the given code base.")
	baseDir                      = flag.String("base_dir", "", "Root location to begin directory traversal.")
	target                       = flag.String("target", "", "Analyze the dependency tree of a specific GN build target.")
	logLevel                     = flag.Int("log_level", 0, "Log level, see https://godoc.org/github.com/golang/glog for more info.")
	exitOnUnlicensedFiles        = flag.Bool("exit_on_unlicensed_files", false, "If true, exits if it encounters files that are unlicensed.")
	exitOnProhibitedLicenseTypes = flag.Bool("exit_on_prohibited_license_types", true, "If true, exits if it encounters a prohibited license type.")
	outputLicenseFile            = flag.Bool("output_license_file", true, "If true, outputs a license file with all the licenses for the project.")
	outputTreeStateFilename      = flag.String("output_tree_state_filename", "", "Filename for saving the state of the file tree.")
	prohibitedLicenseTypes       = flag.String("prohibited_license_types", "", "Comma separated list of license types that are prohibited. This arg is added to the list of prohibitedLicenseTypes in the config file.")
	pproffile                    = flag.String("pprof", "", "generate file that can be parsed by go tool pprof")
	tracefile                    = flag.String("trace", "", "generate file that can be parsed by go tool trace")
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

	if err := config.Init(*configFile); err != nil {
		return fmt.Errorf("failed to initialize config: %s", err)
	}

	if *skipDirs != "" {
		split := strings.Split(*skipDirs, ",")
		for _, s := range split {
			if s != "" {
				config.SkipDirs = append(config.SkipDirs, s)
			}
		}
	}

	additionalSkipDirs := []string{
		"third_party/catapult",                                      // TODO(b/171586646): Remove once completed.
		"third_party/openthread/third_party/nxp",                    // TODO(b/172066115): Remove once completed.
		"third_party/openthread/third_party/openthread-test-driver", // TODO(b/171816602): Remove once completed.
		"third_party/openthread/third_party/ti",                     // TODO(b/172066853): Remove once completed.
		"third_party/vim",                                           // TODO(b/172066343): Remove once completed.
		"third_party/zlib",                                          // TODO(b/172069467): Remove once completed.
		"prebuilt",                                                  // TODO(b/172076124): Remove once completed.
		"prebuilt/third_party/flutter",                              // TODO(b/169676435): Remove once completed.
		"prebuilt/third_party/llvm",                                 // TODO(b/172076113): Remove once completed.
		"prebuilt/third_party/ovmf",                                 // TODO(fxb/59350): Remove once completed.
		"prebuilt/third_party/zlib",                                 // TODO(b/172066115): Remove once completed.
		"prebuilt/virtualization/packages/termina_guest",            // TODO(b/171975485): Remove once completed.
		"zircon/third_party/ulib/musl/third_party",                  // TODO(b/172066115): Remove once completed.
		"zircon/third_party/zstd",                                   // TODO(b/171584331): Remove once completed.
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
	for _, f := range unlicensed {
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

	// TODO(fxb/42986): Remove ExitOnProhibitedLicenseTypes and ExitOnUnlicensedFiles
	// flags once fxb/42986 is completed.
	config.ExitOnProhibitedLicenseTypes = *exitOnProhibitedLicenseTypes
	config.ExitOnUnlicensedFiles = *exitOnUnlicensedFiles

	config.OutputLicenseFile = *outputLicenseFile
	config.OutputTreeStateFilename = *outputTreeStateFilename

	if *licensePatternDir != "" {
		if info, err := os.Stat(*licensePatternDir); os.IsNotExist(err) && info.IsDir() {
			return fmt.Errorf("license pattern directory path %q does not exist!", *licensePatternDir)
		}
		config.LicensePatternDir = *licensePatternDir
	}

	if *baseDir != "" {
		if info, err := os.Stat(*baseDir); os.IsNotExist(err) && info.IsDir() {
			return fmt.Errorf("base directory path %q does not exist!", *baseDir)
		}
		config.BaseDir = *baseDir
	}

	if *target != "" {
		log.Printf("WARNING: Flag \"target\" was set to \"%v\", but that flag is currently unsupported. check-licenses will parse the full directory tree.\n", *target)
		//TODO: enable target-based dependency traversal
		//config.Target = *target
	}

	if err := checklicenses.Walk(context.Background(), &config); err != nil {
		return fmt.Errorf("failed to analyze the given directory: %v", err)
	}
	return nil
}

func main() {
	if err := mainImpl(); err != nil {
		fmt.Fprintf(os.Stderr, "check-licenses: %s\n", err)
		os.Exit(1)
	}
}
