// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package botanist

import (
	"context"
	"fmt"

	"fuchsia.googlesource.com/tools/fastboot"
)

// FastbootToZedboot uses fastboot to boot into Zedboot.
func FastbootToZedboot(ctx context.Context, fastbootTool, zirconRPath string) error {
	f := fastboot.Fastboot{ToolPath: fastbootTool}
	if _, err := f.Continue(ctx); err != nil {
		return fmt.Errorf("failed to boot the device with \"fastboot continue\": %v", err)
	}
	return nil
}
