// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"runtime/pprof"
	"runtime/trace"
)

var (
	logLevel  = flag.Int("log_level", 2, "Log level. Set to 0 for no logs, 1 to log to a file, 2 to log to stdout.")
	pproffile = flag.String("pprof", "", "generate file that can be parsed by go tool pprof")
	tracefile = flag.String("trace", "", "generate file that can be parsed by go tool trace")
)

func setupLogging() error {

	// Log Level setup
	if w, err := getLogWriters(*logLevel, *outDir); err != nil {
		return err
	} else {
		log.SetOutput(w)
	}

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

	// CPU Profiling
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

	return nil
}

// Log == 0: discard all output
// Log == 1: save logs to the outDir folder
// Log == 2: save logs to the outDir folder AND print to stdout
func getLogWriters(logLevel int, outDir string) (io.Writer, error) {

	logTargets := []io.Writer{}

	switch logLevel {
	case 0: // Discard all non-error logs.
		logTargets = append(logTargets, ioutil.Discard)
	case 1: // Write all logs to a log file.
		if outDir != "" {
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
	case 2: // Write all logs to a log file and stdout.
		logTargets = append(logTargets, os.Stdout)
	}

	w := io.MultiWriter(logTargets...)

	return w, nil
}
