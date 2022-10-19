// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
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

	fuchsiaDir     = flag.String("fuchsia_dir", os.Getenv("FUCHSIA_DIR"), "Location of the fuchsia root directory (//).")
	buildDir       = flag.String("build_dir", os.Getenv("FUCHSIA_BUILD_DIR"), "Location of GN build directory.")
	outDir         = flag.String("out_dir", "/tmp/check-licenses", "Directory to write outputs to.")
	licensesOutDir = flag.String("licenses_out_dir", "", "Directory to write license text segments.")

	buildInfoVersion = flag.String("build_info_version", "version", "Version of fuchsia being built. Used for uploading results.")
	buildInfoProduct = flag.String("build_info_product", "product", "Version of fuchsia being built. Used for uploading results.")
	buildInfoBoard   = flag.String("build_info_board", "board", "Version of fuchsia being built. Used for uploading results.")

	gnPath    = flag.String("gn_path", "{FUCHSIA_DIR}/prebuilt/third_party/gn/linux-x64/gn", "Path to GN executable. Required when target is specified.")
	gnGenFile = flag.String("gn_gen_file", "{BUILD_DIR}/project.json", "Path to 'project.json' output file.")

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
		*fuchsiaDir = "."
	}
	if *fuchsiaDir, err = filepath.Abs(*fuchsiaDir); err != nil {
		return fmt.Errorf("Failed to get absolute directory for *fuchsiaDir %v: %v", *fuchsiaDir, err)
	}
	ConfigVars["{FUCHSIA_DIR}"] = *fuchsiaDir

	// buildDir
	if *buildDir == "" && *outputLicenseFile {
		return fmt.Errorf("--build_dir cannot be empty.")
	}
	if *buildDir, err = filepath.Abs(*buildDir); err != nil {
		return fmt.Errorf("Failed to get absolute directory for *buildDir%v: %v", *buildDir, err)
	}
	ConfigVars["{BUILD_DIR}"] = *buildDir

	// outDir
	rootOutDir := *outDir
	if *outDir != "" {
		*outDir, err = filepath.Abs(*outDir)
		if err != nil {
			return fmt.Errorf("Failed to get absolute directory for *outDir %v: %v", *outDir, err)
		}
		rootOutDir = *outDir

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
	ConfigVars["{ROOT_OUT_DIR}"] = rootOutDir

	// licensesOutDir
	if *licensesOutDir != "" {
		*licensesOutDir, err = filepath.Abs(*licensesOutDir)
		if err != nil {
			return fmt.Errorf("Failed to get absolute directory for *licensesOutDir %v: %v", *licensesOutDir, err)
		}
		if _, err := os.Stat(*licensesOutDir); os.IsNotExist(err) {
			err := os.MkdirAll(*licensesOutDir, 0755)
			if err != nil {
				return fmt.Errorf("Failed to create licenses out directory [%v]: %v\n", licensesOutDir, err)
			}
		}
	}
	ConfigVars["{LICENSES_OUT_DIR}"] = *licensesOutDir

	// gnPath
	if *gnPath == "" && *outputLicenseFile {
		return fmt.Errorf("--gn_path cannot be empty.")
	}
	*gnPath = strings.ReplaceAll(*gnPath, "{FUCHSIA_DIR}", *fuchsiaDir)
	*gnPath, err = filepath.Abs(*gnPath)
	if err != nil {
		return fmt.Errorf("Failed to get absolute directory for *gnPath %v: %v", *gnPath, err)
	}

	*gnGenFile = strings.ReplaceAll(*gnGenFile, "{BUILD_DIR}", *buildDir)
	*gnGenFile, err = filepath.Abs(*gnGenFile)
	if err != nil {
		return fmt.Errorf("Failed to get absolute directory for *gnGenFile %v: %v", *gnGenFile, err)
	}

	ConfigVars["{GN_PATH}"] = *gnPath
	ConfigVars["{GN_GEN_FILE}"] = *gnPath
	ConfigVars["{OUTPUT_LICENSE_FILE}"] = strconv.FormatBool(*outputLicenseFile)

	// target
	if flag.NArg() > 1 {
		return fmt.Errorf("check-licenses takes a maximum of 1 positional argument (filepath or gn target), got %v\n", flag.NArg())
	}
	if flag.NArg() == 1 {
		target = flag.Arg(0)
	}
	ConfigVars["{TARGET}"] = target

	// diffTarget
	if *diffTarget != "" {
		*diffTarget, err = filepath.Abs(*diffTarget)
		if err != nil {
			return fmt.Errorf("Failed to get absolute directory for *diffTarget %v: %v", *diffTarget, err)
		}
	}
	ConfigVars["{DIFF_TARGET}"] = *diffTarget

	// configFile
	configFile := strings.ReplaceAll(defaultConfigFile, "{FUCHSIA_DIR}", *fuchsiaDir)
	Config, err = NewCheckLicensesConfig(configFile)
	if err != nil {
		return err
	}

	if err := os.Chdir(*fuchsiaDir); err != nil {
		return err
	}

	if err := Execute(context.Background()); err != nil {
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
