// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

const (
	defaultConfigFile = "{FUCHSIA_DIR}/tools/check-licenses/cmd/_config.json"
)

var (
	Config *CheckLicensesConfig

	target string
)

var (
	configFile_deprecated = flag.String("config_file", "", "Deprecated, but kept around for backwards compatibility.")
	filter                = flag.String("filter", "", "Files that contain dependencies of the target or workspace. Used to filter projects in the final NOTICE file.")

	diffTarget = flag.String("diff_target", "", "Notice file to diff the current licenses against.")

	fuchsiaDir = flag.String("fuchsia_dir", os.Getenv("FUCHSIA_DIR"), "Location of the fuchsia root directory (//).")
	buildDir   = flag.String("build_dir", os.Getenv("FUCHSIA_BUILD_DIR"), "Location of GN build directory.")
	outDir     = flag.String("out_dir", "/tmp/check-licenses", "Directory to write outputs to.")

	buildInfoVersion = flag.String("build_info_version", "version", "Version of fuchsia being built. Used for uploading results.")
	buildInfoProduct = flag.String("build_info_product", "product", "Version of fuchsia being built. Used for uploading results.")
	buildInfoBoard   = flag.String("build_info_board", "board", "Version of fuchsia being built. Used for uploading results.")

	gnPath = flag.String("gn_path", "{FUCHSIA_DIR}/prebuilt/third_party/gn/linux-x64/gn", "Path to GN executable. Required when target is specified.")

	outputLicenseFile = flag.Bool("output_license_file", true, "Flag for enabling template expansions.")
)

func mainImpl() error {
	var err error

	flag.Parse()

	// buildInfo
	ConfigVars["{BUILD_INFO_VERSION}"] = *buildInfoVersion

	if *buildInfoProduct == "product" {
		b, err := os.ReadFile(filepath.Join(*buildDir, "product.txt"))
		if err == nil {
			*buildInfoProduct = string(b)
		}
	}
	ConfigVars["{BUILD_INFO_PRODUCT}"] = *buildInfoProduct

	if *buildInfoBoard == "board" {
		b, err := os.ReadFile(filepath.Join(*buildDir, "board.txt"))
		if err == nil {
			*buildInfoBoard = string(b)
		}
	}
	ConfigVars["{BUILD_INFO_BOARD}"] = *buildInfoBoard

	// fuchsiaDir
	if *fuchsiaDir == "" {
		// TODO: Update CQ to provide the fuchsia home directory.
		//return fmt.Errorf("--fuchsia_dir cannot be empty.")
		*fuchsiaDir = "."
	}
	if *fuchsiaDir, err = filepath.Abs(*fuchsiaDir); err != nil {
		return fmt.Errorf("Failed to get absolute directory for *fuchsiaDir %v: %v", *fuchsiaDir, err)
	}
	ConfigVars["{FUCHSIA_DIR}"] = *fuchsiaDir

	// diffTarget
	if *diffTarget != "" {
		*diffTarget, err = filepath.Abs(*diffTarget)
		if err != nil {
			return fmt.Errorf("Failed to get absolute directory for *diffTarget %v: %v", *diffTarget, err)
		}
	}

	ConfigVars["{DIFF_TARGET}"] = *diffTarget

	// buildDir
	if *buildDir == "" && *outputLicenseFile {
		return fmt.Errorf("--build_dir cannot be empty.")
	}
	if *buildDir, err = filepath.Abs(*buildDir); err != nil {
		return fmt.Errorf("Failed to get absolute directory for *buildDir%v: %v", *buildDir, err)
	}
	ConfigVars["{BUILD_DIR}"] = *buildDir

	// outDir
	if *outDir != "" {
		*outDir, err = filepath.Abs(*outDir)
		if err != nil {
			return fmt.Errorf("Failed to get absolute directory for *outDir %v: %v", *outDir, err)
		}

		if *outputLicenseFile {
			productBoard := fmt.Sprintf("%v.%v", *buildInfoProduct, *buildInfoBoard)
			*outDir = filepath.Join(*outDir, *buildInfoVersion, productBoard)
		} else {
			*outDir = filepath.Join(*outDir, *buildInfoVersion, "everything")
		}
	}
	if _, err := os.Stat(*outDir); os.IsNotExist(err) {
		err := os.MkdirAll(*outDir, 0755)
		if err != nil {
			return fmt.Errorf("Failed to create out directory [%v]: %v\n", outDir, err)
		}
	}
	ConfigVars["{OUT_DIR}"] = *outDir

	// gnPath
	if *gnPath == "" && *outputLicenseFile {
		return fmt.Errorf("--gn_path cannot be empty.")
	}
	*gnPath = strings.ReplaceAll(*gnPath, "{FUCHSIA_DIR}", *fuchsiaDir)
	*gnPath, err = filepath.Abs(*gnPath)
	if err != nil {
		return fmt.Errorf("Failed to get absolute directory for *gnPath %v: %v", *gnPath, err)
	}

	ConfigVars["{GN_PATH}"] = *gnPath
	ConfigVars["{OUTPUT_LICENSE_FILE}"] = strconv.FormatBool(*outputLicenseFile)

	// logLevel
	w, err := getLogWriters(*logLevel, *outDir)
	if err != nil {
		return err
	}
	log.SetOutput(w)

	// target
	if flag.NArg() > 1 {
		return fmt.Errorf("check-licenses takes a maximum of 1 positional argument (filepath or gn target), got %v\n", flag.NArg())
	}
	if flag.NArg() == 1 {
		target = flag.Arg(0)
	}
	if *outputLicenseFile {
		if isPath(target) {
			target, _ = filepath.Abs(target)
		} else {
			// Run "fx gn <>" command to generate a filter file.
			gn, err := NewGn(*gnPath, *buildDir)
			if err != nil {
				return err
			}

			filterDir := filepath.Join(*outDir, "filter")
			gnFilterFile := filepath.Join(filterDir, "gnFilter.json")
			if _, err := os.Stat(filterDir); os.IsNotExist(err) {
				err := os.Mkdir(filterDir, 0755)
				if err != nil {
					return fmt.Errorf("Failed to create filter directory [%v]: %v\n", filterDir, err)
				}
			}

			startGn := time.Now()
			if target != "" {
				log.Printf("Running 'fx gn desc %v' command...", target)
				if err := gn.Dependencies(context.Background(), gnFilterFile, target); err != nil {
					return err
				}
			} else {
				log.Print("Running 'fx gn gen' command...")
				if err := gn.Gen(context.Background(), gnFilterFile); err != nil {
					return err
				}
			}
			if *filter == "" {
				*filter = gnFilterFile
			} else {
				*filter = fmt.Sprintf("%v,%v", gnFilterFile, *filter)
			}
			log.Printf("Done. [%v]\n", time.Since(startGn))
		}
	}
	ConfigVars["{TARGET}"] = target

	// configFile
	configFile := strings.ReplaceAll(defaultConfigFile, "{FUCHSIA_DIR}", *fuchsiaDir)
	Config, err = NewCheckLicensesConfig(configFile)
	if err != nil {
		return err
	}

	// Set non-string config values directly.
	Config.Result.OutputLicenseFile = *outputLicenseFile
	Config.OutputLicenseFile = *outputLicenseFile
	Config.World.Filters = strings.Split(*filter, ",")
	Config.Filters = strings.Split(*filter, ",")

	if err := os.Chdir(*fuchsiaDir); err != nil {
		return err
	}

	if err := Execute(context.Background(), Config); err != nil {
		return fmt.Errorf("failed to analyze the given directory: %v", err)
	}
	return nil
}

func isPath(target string) bool {
	if strings.HasPrefix(target, "//") {
		return false
	}
	if strings.HasPrefix(target, ":") {
		return false
	}
	if target == "" {
		return false
	}
	return true
}

func main() {
	if err := mainImpl(); err != nil {
		fmt.Fprintf(os.Stderr, "check-licenses: %s\nSee go/fuchsia-licenses-playbook for information on resolving common errors.\n", err)
		os.Exit(1)
	}
}
