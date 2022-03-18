// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"fmt"
	"runtime/trace"
	"time"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/filetree"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/license"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/result"
)

func Execute(ctx context.Context, config *CheckLicensesConfig) error {
	start := time.Now()

	startInitialize := time.Now()
	fmt.Print("Initializing... ")
	if err := initialize(config); err != nil {
		fmt.Println("Error!")
		return err
	}
	fmt.Printf("Done. [%v]\n", time.Since(startInitialize))

	r := trace.StartRegion(ctx, "filetree.NewFileTree("+config.Target+")")
	startFileTree := time.Now()
	fmt.Print("Discovering files and folders... ")
	_, err := filetree.NewFileTree(config.Target, nil)
	if err != nil {
		fmt.Println("Error!")
		return err
	}
	fmt.Printf("Done. [%v]\n", time.Since(startFileTree))
	r.End()

	r = trace.StartRegion(ctx, "project.AnalyzeLicenses")
	startAnalyze := time.Now()
	fmt.Printf("Searching for license texts [%v projects]... ", len(project.AllProjects))
	err = project.AnalyzeLicenses()
	if err != nil {
		fmt.Println("Error!")
		return err
	}
	fmt.Printf("Done. [%v]\n", time.Since(startAnalyze))
	r.End()

	r = trace.StartRegion(ctx, "result.SaveResults")
	startSaveResults := time.Now()
	fmt.Print("Saving results... ")
	var s string
	s, err = result.SaveResults()
	if err != nil {
		return err
	}
	fmt.Printf("Done. [%v]\n", time.Since(startSaveResults))
	r.End()

	fmt.Printf("\nTotal runtime: %v\n============\n", time.Since(start))
	fmt.Println(s)
	return nil
}

func initialize(c *CheckLicensesConfig) error {
	if err := file.Initialize(c.File); err != nil {
		return err
	}
	if err := license.Initialize(c.License); err != nil {
		return err
	}
	if err := project.Initialize(c.Project); err != nil {
		return err
	}
	if err := filetree.Initialize(c.FileTree); err != nil {
		return err
	}
	if err := result.Initialize(c.Result); err != nil {
		return err
	}

	return nil
}
