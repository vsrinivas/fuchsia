// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"
	"os"

	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/hostplatform"
	"go.fuchsia.dev/fuchsia/tools/lib/runner"
)

// Build runs `ninja` given a static and context spec. It's intended to be
// consumed as a library function.
func Build(ctx context.Context, staticSpec *fintpb.Static, contextSpec *fintpb.Context) error {
	platform, err := hostplatform.Name()
	if err != nil {
		return err
	}

	runner := &runner.SubprocessRunner{}
	targets := constructNinjaTargets(staticSpec)

	ninjaPath := thirdPartyPrebuilt(contextSpec.CheckoutDir, platform, "ninja")
	cmd := []string{ninjaPath, "-C", contextSpec.BuildDir}
	cmd = append(cmd, targets...)
	return runner.Run(ctx, cmd, os.Stdout, os.Stderr)
}

func constructNinjaTargets(*fintpb.Static) []string {
	return nil
}
