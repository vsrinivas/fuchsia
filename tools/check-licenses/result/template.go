// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package result

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/result/world"
)

// Generate output files for every template defined in the config file.
func expandTemplates() (string, error) {
	if Config.OutDir == "" {
		return "", nil
	}

	expansionOutDir := filepath.Join(Config.OutDir, "out")
	if _, err := os.Stat(expansionOutDir); os.IsNotExist(err) {
		err := os.Mkdir(expansionOutDir, 0755)
		if err != nil {
			return "", fmt.Errorf("Failed to make directory %v: %v", expansionOutDir, err)
		}
	}

	var b strings.Builder
	b.WriteString("\n")

	w, err := world.NewWorld()
	if err != nil {
		return "", err
	}
	b.WriteString(w.Status.String())
	b.WriteString("\n")

	for _, o := range Config.Outputs {
		if t, ok := AllTemplates[o]; !ok {
			return "", fmt.Errorf("Couldn't find template %v\n", o)
		} else {
			name := filepath.Join(expansionOutDir, o)
			f, err := os.Create(name)
			if err != nil {
				return "", fmt.Errorf("Failed to create template file %v: %v", name, err)
			}
			if err := t.Execute(f, w); err != nil {
				return "", fmt.Errorf("Failed to expand template %v: %v", name, err)
			}
			if Config.Zip {
				if err := compressGZ(name); err != nil {
					return "", fmt.Errorf("Failed to compress template %v: %v", name, err)
				}
			}
			b.WriteString(fmt.Sprintf(" ⦿ Executed template -> %v", name))
			if Config.Zip {
				b.WriteString(" (+ *.gz)")
			}
			b.WriteString("\n")
		}
	}
	return b.String(), nil
}
