// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil"
)

// AddFFXDeps selects and adds the files needed to run `ffx emu` to
// the shard's list of dependencies.
func AddFFXDeps(s *Shard, buildDir string) error {
	if len(s.Tests) == 0 {
		return fmt.Errorf("shard %s has no tests", s.Name)
	}
	if s.Env.IsEmu {
		targetCPU := s.Tests[0].CPU
		deps, err := ffxutil.GetEmuDeps(buildDir, targetCPU, []string{})
		if err != nil {
			return err
		}
		s.AddDeps(deps)
	}
	return nil
}
