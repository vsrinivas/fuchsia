// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/streams"
	"go.fuchsia.dev/fuchsia/tools/staticanalysis"
	"go.fuchsia.dev/fuchsia/tools/staticanalysis/analyzers/clippy"
	"go.fuchsia.dev/fuchsia/tools/staticanalysis/analyzers/codelinks"
	"go.fuchsia.dev/fuchsia/tools/staticanalysis/analyzers/rfcmeta"
)

// fileToAnalyze is the expected schema of each element in the file specified by
// `-files-json`, indicating the files to analyze.
type fileToAnalyze struct {
	// Path is the file's path relative to the checkout root.
	Path string `json:"path"`
}

func getAnalyzers(checkoutDir, buildDir string) ([]staticanalysis.Analyzer, error) {
	modules, err := build.NewModules(buildDir)
	if err != nil {
		return nil, err
	}

	var res []staticanalysis.Analyzer

	clippy, err := clippy.New(checkoutDir, modules)
	if err != nil {
		return nil, err
	}
	res = append(res, clippy)

	res = append(res, codelinks.New(checkoutDir))
	res = append(res, rfcmeta.New(checkoutDir))

	return res, nil
}

func runAnalyzers(ctx context.Context, analyzers []staticanalysis.Analyzer, files []string) ([]*staticanalysis.Finding, error) {
	// Empty slice rather than nil so it gets marshaled to an empty JSON array
	// instead of null.
	allFindings := []*staticanalysis.Finding{}

	var errs []error

	for _, analyzer := range analyzers {
		for _, f := range files {
			findings, err := analyzer.Analyze(ctx, f)
			if err != nil {
				logger.Errorf(ctx, err.Error())
				errs = append(errs, err)
				// It's possible for an analyzer to produce findings *and* an
				// error in which case we still want to emit the findings (but
				// produce a non-zero retcode).
			}
			for _, finding := range findings {
				if err := finding.Normalize(); err != nil {
					logger.Errorf(ctx, err.Error())
					errs = append(errs, err)
				}
			}
			allFindings = append(allFindings, findings...)
		}
	}

	if len(errs) > 0 {
		return allFindings, errs[0]
	}
	return allFindings, nil
}

func mainImpl(ctx context.Context) error {
	ctx, cancel := signal.NotifyContext(ctx, syscall.SIGTERM, syscall.SIGINT)
	defer cancel()

	var flags struct {
		checkoutDir string
		buildDir    string
		filesJSON   string
		outputJSON  string
	}

	flag.StringVar(&flags.buildDir, "build-dir", "", "Fuchsia build directory.")
	flag.StringVar(&flags.checkoutDir, "checkout-dir", "", "Fuchsia checkout directory.")
	flag.StringVar(&flags.filesJSON, "files-json", "", "JSON manifest of files to analyze. If unset, reads from positional arguments.")
	flag.StringVar(&flags.outputJSON, "output-json", "", "File to write findings to. If unspecified, findings will be written to stdout.")
	flag.Parse()

	if flags.buildDir == "" {
		return errors.New("-build-dir is required")
	}
	if flags.checkoutDir == "" {
		return errors.New("-checkout-dir is required")
	}

	var files []string
	if flags.filesJSON != "" {
		var filesToAnalyze []fileToAnalyze
		if err := jsonutil.ReadFromFile(flags.filesJSON, &filesToAnalyze); err != nil {
			return fmt.Errorf("failed to read -files-json: %w", err)
		}
		if flag.NArg() > 0 {
			return errors.New("positional arguments not accepted when -files-json is specified")
		}
		for _, f := range filesToAnalyze {
			files = append(files, f.Path)
		}
	} else if flag.NArg() > 0 {
		files = flag.Args()
	} else {
		logger.Infof(ctx, "No files specified. Either use -files-json or pass paths as positional arguments.")
		return nil
	}

	analyzers, err := getAnalyzers(flags.checkoutDir, flags.buildDir)
	if err != nil {
		return err
	}

	findings, finalErr := runAnalyzers(ctx, analyzers, files)
	// Attempt to report findings even if some analyzers failed, since others
	// might have passed.
	if err := writeFindings(ctx, findings, flags.outputJSON); err != nil {
		if finalErr == nil {
			finalErr = err
		}
	}
	return finalErr
}

func writeFindings(ctx context.Context, findings []*staticanalysis.Finding, outputJSON string) error {
	output := streams.Stdout(ctx)
	if outputJSON != "" {
		f, err := os.Create(outputJSON)
		if err != nil {
			return err
		}
		defer f.Close()
		output = f
	}
	enc := json.NewEncoder(output)
	enc.SetIndent("", "  ")
	if err := enc.Encode(findings); err != nil {
		return fmt.Errorf("failed to write findings: %w", err)
	}
	return nil
}

func main() {
	const logFlags = log.Ltime | log.Lmicroseconds | log.Lshortfile
	log := logger.NewLogger(logger.DebugLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "staticanalysis ")
	log.SetFlags(logFlags)
	ctx := logger.WithLogger(context.Background(), log)

	if err := mainImpl(ctx); err != nil {
		logger.Fatalf(ctx, err.Error())
		os.Exit(1)
	}
}
