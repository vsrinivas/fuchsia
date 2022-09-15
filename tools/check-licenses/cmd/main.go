// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"runtime/pprof"
	"runtime/trace"
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

	gnPath = flag.String("gn_path", "{FUCHSIA_DIR}/prebuilt/third_party/gn/linux-x64/gn", "Path to GN executable. Required when target is specified.")

	logLevel  = flag.Int("log_level", 2, "Log level. Set to 0 for no logs, 1 to log to a file, 2 to log to stdout.")
	pproffile = flag.String("pprof", "", "generate file that can be parsed by go tool pprof")
	tracefile = flag.String("trace", "", "generate file that can be parsed by go tool trace")

	outputLicenseFile = flag.Bool("output_license_file", true, "Flag for enabling template expansions.")
)

func mainImpl() error {
	var err error

	flag.Parse()

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
	Config.World.Filters = strings.Split(*filter, ",")

	// Tracing
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

// Log == 0: discard all output
// Log == 1: save logs to the outDir folder
// Log == 2: save logs to the outDir folder AND print to stdout
func getLogWriters(logLevel int, outDir string) (io.Writer, error) {
	logTargets := []io.Writer{}
	if logLevel == 0 {
		// Default: logLevel == 0
		// Discard all non-error logs.
		logTargets = append(logTargets, io.Discard)
	} else {
		if logLevel == 1 && outDir != "" {
			// logLevel == 1
			// Write all logs to a log file.
			if _, err := os.Stat(outDir); os.IsNotExist(err) {
				err := os.Mkdir(outDir, 0755)
				if err != nil {
					return nil, fmt.Errorf("Failed to create out directory [%v]: %v\n", outDir, err)
				}
			}
			logfilePath := filepath.Join(outDir, "logs")
			f, err := os.OpenFile(logfilePath, os.O_RDWR|os.O_CREATE|os.O_APPEND, 0666)
			if err != nil {
				return nil, fmt.Errorf("Failed to create log file [%v]: %v\n", logfilePath, err)
			}
			defer f.Close()
			logTargets = append(logTargets, f)
		}
		if logLevel == 2 {
			// logLevel == 2
			// Write all logs to a log file and stdout.
			logTargets = append(logTargets, os.Stdout)
		}
	}
	w := io.MultiWriter(logTargets...)

	return w, nil
}

func main() {
	if err := mainImpl(); err != nil {
		fmt.Fprintf(os.Stderr, "check-licenses: %s\nSee go/fuchsia-licenses-playbook for information on resolving common errors.\n", err)
		os.Exit(1)
	}
}
