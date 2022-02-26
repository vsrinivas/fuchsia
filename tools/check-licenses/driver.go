// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"fmt"
	"runtime/trace"
	"time"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/filetree"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/results"
)

func Execute(ctx context.Context, config *Config) error {
	start := time.Now()

	r := trace.StartRegion(ctx, "NewFileTree("+config.BaseDir+")")
	_, err := filetree.NewFileTree(ctx, config.BaseDir, nil)
	if err != nil {
		return err
	}
	r.End()

	elapsed := time.Since(start)
	fmt.Println(elapsed)

	if err := results.SaveResults(); err != nil {
		return err
	}

	return nil
}
