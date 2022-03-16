// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"fmt"
	//"runtime/trace"
	"time"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/license"
	//"go.fuchsia.dev/fuchsia/tools/check-licenses/filetree"
	//"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
	//"go.fuchsia.dev/fuchsia/tools/check-licenses/result"
)

func Execute(ctx context.Context, config *CheckLicensesConfig) error {
	start := time.Now()

	startInitialize := time.Now()
	fmt.Print("Initializing... ")
	if err := initialize(config); err != nil {
		return err
	}
	fmt.Printf("Done. [%v]\n", time.Since(startInitialize))

	/*
		// Traverse the file system, creating a file tree that represents
		// all files and folders from the target directory.
		r := trace.StartRegion(ctx, "filetree.NewFileTree("+config.Target+")")
		_, err := filetree.NewFileTree(config.Target, nil)
		if err != nil {
			return err
		}
		r.End()

		// Analyze the LICENSE and NOTICE files found in the previous step.
		r = trace.StartRegion(ctx, "project.AnalyzeLicenses")
		err = project.AnalyzeLicenses()
		if err != nil {
			return err
		}
		r.End()
	*/

	//if err := result.SaveResults(); err != nil {
	//	return err
	//}

	fmt.Printf("Total runtime: %v\n", time.Since(start))
	return nil
}

func initialize(c *CheckLicensesConfig) error {
	if err := file.Initialize(c.File); err != nil {
		return err
	}
	if err := license.Initialize(c.License); err != nil {
		return err
	}
	/*
		if err := project.Initialize(c.Project); err != nil {
			return err
		}
		if err := filetree.Initialize(c.FileTree); err != nil {
			return err
		}
		if err := result.Initialize(c.Result); err != nil {
			return err
		}*/

	return nil
}
