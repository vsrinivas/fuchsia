// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package results

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/filetree"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

const (
	indent = "  "
)

// SaveResults saves the results to the output files defined in the config file.
func SaveResults() error {
	var b strings.Builder

	s, err := savePackageInfo("project", project.Config, project.Metrics)
	if err != nil {
		return err
	}
	b.WriteString(s)

	s, err = savePackageInfo("filetree", filetree.Config, filetree.Metrics)
	if err != nil {
		return err
	}
	b.WriteString(s)

	if err = writeFile("summary", []byte(b.String())); err != nil {
		return err
	}

	fmt.Println(b.String())
	fmt.Printf("Full summary and output files -> %s\n", Config.OutputDir)
	return nil
}

// This retrieves all the relevant metrics information for a given package.
// e.g. the //tools/check-licenses/filetree package.
func savePackageInfo(pkgName string, c interface{}, m MetricsInterface) (string, error) {
	var b strings.Builder

	fmt.Fprintf(&b, "\n%s Metrics:\n", strings.Title(pkgName))

	counts := m.Counts()
	keys := make([]string, 0, len(counts))
	for k := range counts {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	for _, k := range keys {
		fmt.Fprintf(&b, "%s%s: %s\n", indent, k, strconv.Itoa(counts[k]))
	}
	if err := saveConfig(pkgName, c); err != nil {
		return "", err
	}
	if err := saveMetrics(pkgName, m); err != nil {
		return "", err
	}
	return b.String(), nil
}

// Save the config files so we can recreate this run in the future.
func saveConfig(pkg string, c interface{}) error {
	if bytes, err := json.MarshalIndent(c, "", "  "); err != nil {
		return err
	} else {
		return writeFile(filepath.Join(pkg, "_config.json"), bytes)
	}
}

// Save the "Values" metrics: freeform data stored in a map with string keys.
func saveMetrics(pkg string, m MetricsInterface) error {
	for k, v := range m.Values() {
		if bytes, err := json.MarshalIndent(v, "", "  "); err != nil {
			return err
		} else {
			k = strings.Replace(k, " ", "_", -1)
			if err := writeFile(filepath.Join(pkg, k), bytes); err != nil {
				return err
			}
		}
	}
	return nil
}

func writeFile(path string, data []byte) error {
	path = filepath.Join(Config.OutputDir, path)
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return err
	}
	return os.WriteFile(path, data, 0666)
}
