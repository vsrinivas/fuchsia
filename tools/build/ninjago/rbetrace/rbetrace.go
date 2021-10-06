// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// package rbetrace contains utilities for working with RBE traces.
package rbetrace

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build/ninjago/chrometrace"
)

// This function is stubbable in test.
var runRPL2TraceCommand = func(ctx context.Context, rpl2TraceBin, rplPath, jsonPath string) error {
	cmd := exec.CommandContext(
		ctx,
		rpl2TraceBin,
		"--log_path",
		"text://"+rplPath,
		"--output",
		jsonPath,
	)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("converting RPL %q to Chrome trace: %v", rplPath, err)
	}
	return nil
}

// FromRPLFile converts reproxy's RPL file containing reproxy's performance
// metrics to Chrome trace format.
//
// This function relies on reclient's rpl2trace binary for parsing RPL files,
// because currently there are no easy ways to depend on the proto schema
// reclient uses to write RPL files (RPL files are textprotos).
func FromRPLFile(ctx context.Context, rpl2TraceBin, rplPath, tmpDir string) ([]chrometrace.Trace, error) {
	tmpJSONPath := filepath.Join(tmpDir, "rbe_traces.json")
	if err := runRPL2TraceCommand(ctx, rpl2TraceBin, rplPath, tmpJSONPath); err != nil {
		return nil, err
	}

	var rbeTrace []chrometrace.Trace
	tmpJSON, err := os.Open(tmpJSONPath)
	if err != nil {
		return nil, fmt.Errorf("reading %q: %v", tmpJSONPath, err)
	}
	d := json.NewDecoder(tmpJSON)
	if err := d.Decode(&rbeTrace); err != nil {
		return nil, fmt.Errorf("parsing %q: %v", tmpJSONPath, err)
	}
	return rbeTrace, nil
}
