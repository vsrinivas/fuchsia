// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"io"
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
	configFile = flag.String("config_file", "{FUCHSIA_DIR}/tools/check-licenses/_config.json", "Default config file.")

	diffTarget = flag.String("diff_target", "", "Notice file to diff the current licenses against")

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
	fuchsiaDirUpdate := ""
	if *fuchsiaDir == "" {
		// TODO: Update CQ to provide the fuchsia home directory.
		//return fmt.Errorf("--fuchsia_dir cannot be empty.")
		*fuchsiaDir = "."
	}
	if fuchsiaDirUpdate, err = filepath.Abs(*fuchsiaDir); err != nil {
		return err
	}
	checklicenses.ConfigVars["{FUCHSIA_DIR}"] = fuchsiaDirUpdate

	// target
	target := ""
	if flag.NArg() > 1 {
		return fmt.Errorf("check-licenses takes a maximum of 1 positional argument (filepath or gn target), got %v\n", flag.NArg())
	}
	if flag.NArg() == 1 {
		target = flag.Arg(0)
	}
	if isPath(target) {
		var err error
		if target, err = filepath.Abs(target); err != nil {
			return err
		}
	}
	checklicenses.ConfigVars["{TARGET}"] = target

	// diffTarget
	diffTargetUpdate := ""
	if *diffTarget != "" {
		if diffTargetUpdate, err = filepath.Abs(*diffTarget); err != nil {
			return err
		}
	}
	checklicenses.ConfigVars["{DIFF_TARGET}"] = diffTargetUpdate

	// buildDir
	buildDirUpdate := ""
	if *buildDir == "" && *outputLicenseFile {
		return fmt.Errorf("--build_dir cannot be empty.")
	}
	if buildDirUpdate, err = filepath.Abs(*buildDir); err != nil {
		return err
	}
	checklicenses.ConfigVars["{BUILD_DIR}"] = buildDirUpdate

	// outDir
	outDirUpdate := ""
	if *outDir != "" {
		if outDirUpdate, err = filepath.Abs(*outDir); err != nil {
			return err
		}
	}
	checklicenses.ConfigVars["{OUT_DIR}"] = outDirUpdate

	// gnPath
	gnPathUpdate := *gnPath
	if *gnPath == "" {
		return fmt.Errorf("--gn_path cannot be empty.")
	}
	gnPathUpdate = strings.ReplaceAll(gnPathUpdate, "{FUCHSIA_DIR}", fuchsiaDirUpdate)
	checklicenses.ConfigVars["{GN_PATH}"] = gnPathUpdate

	// logLevel
	w, err := getLogWriters(*logLevel, outDirUpdate)
	if err != nil {
		return err
	}
	log.SetOutput(w)

	configFileUpdate := strings.ReplaceAll(*configFile, "{FUCHSIA_DIR}", fuchsiaDirUpdate)
	config, err := checklicenses.NewCheckLicensesConfig(configFileUpdate)
	if err != nil {
		return err
	}
	config.Result.OutputLicenseFile = *outputLicenseFile

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

	if err := checklicenses.Execute(context.Background(), config); err != nil {
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

func getLogWriters(logLevel int, outDir string) (io.Writer, error) {
	logTargets := []io.Writer{}
	if logLevel == 0 {
		// Default: logLevel == 0
		// Discard all non-error logs.
		logTargets = append(logTargets, ioutil.Discard)
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
