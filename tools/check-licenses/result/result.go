// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package result

import (
	"bytes"
	"compress/gzip"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/directory"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/license"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/result/world"
)

const (
	indent = "  "
)

// SaveResults saves the results to the output files defined in the config file.
func SaveResults(cmdConfig interface{}, cmdMetrics MetricsInterface) (string, error) {
	var b strings.Builder

	s, err := savePackageInfo("cmd", cmdConfig, cmdMetrics)
	if err != nil {
		return "", err
	}
	b.WriteString(s)

	s, err = savePackageInfo("license", license.Config, license.Metrics)
	if err != nil {
		return "", err
	}
	b.WriteString(s)

	s, err = savePackageInfo("project", project.Config, project.Metrics)
	if err != nil {
		return "", err
	}
	b.WriteString(s)

	s, err = savePackageInfo("file", file.Config, file.Metrics)
	if err != nil {
		return "", err
	}
	b.WriteString(s)

	s, err = savePackageInfo("directory", directory.Config, directory.Metrics)
	if err != nil {
		return "", err
	}
	b.WriteString(s)

	s, err = savePackageInfo("result", Config, Metrics)
	if err != nil {
		return "", err
	}
	b.WriteString(s)

	err = RunChecks()
	if err != nil {
		if Config.ExitOnError {
			return "", err
		} else {
			// TODO: Log err to a file
		}
	}

	if Config.OutputLicenseFile {
		s1, err := expandTemplates()
		if err != nil {
			return "", err
		}

		s2, err := savePackageInfo("world", world.Config, world.Metrics)
		if err != nil {
			return "", err
		}

		b.WriteString(s2)
		b.WriteString(s1)
	} else {
		b.WriteString("Not expanding templates.\n")
	}

	if err = writeFile("summary", []byte(b.String())); err != nil {
		return "", err
	}

	b.WriteString("\n")
	if Config.OutDir != "" {
		b.WriteString(fmt.Sprintf("Full summary and output files -> %s\n", Config.OutDir))
	} else {
		b.WriteString("Set the 'outputdir' arg in the config file to save detailed information to disk.\n")
	}

	return b.String(), nil
}

// This retrieves all the relevant metrics information for a given package.
// e.g. the //tools/check-licenses/directory package.
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
	if Config.OutDir != "" {
		if _, err := os.Stat(Config.OutDir); os.IsNotExist(err) {
			err := os.MkdirAll(Config.OutDir, 0755)
			if err != nil {
				return "", fmt.Errorf("Failed to make directory %v: %v", Config.OutDir, err)
			}
		}

		if err := saveMetrics(pkgName, m); err != nil {
			return "", err
		}
	}
	return b.String(), nil
}

// Save the "Files" and "Values" metrics: freeform data stored in a map with string keys.
func saveMetrics(pkg string, m MetricsInterface) error {
	for k, bytes := range m.Files() {

		// Spaces and commas are not allowed in file or folder names.
		// Replace spaces and commas with underscores.
		k = strings.Replace(k, " ", "_", -1)
		k = strings.Replace(k, ",", "_", -1)

		path := filepath.Join(pkg, k)
		if err := writeFile(path, bytes); err != nil {
			return fmt.Errorf("Failed to write Files file %v: %v", path, err)
		}
	}

	for k, v := range m.Values() {
		sort.Strings(v)
		if bytes, err := json.MarshalIndent(v, "", "  "); err != nil {
			return fmt.Errorf("Failed to marshal indent for key %v: %v", k, err)
		} else {
			k = strings.Replace(k, " ", "_", -1)
			path := filepath.Join(pkg, k)
			if err := writeFile(path, bytes); err != nil {
				return fmt.Errorf("Failed to write Values file %v: %v", path, err)
			}
		}
	}
	return nil
}

func writeFile(path string, data []byte) error {
	path = filepath.Join(Config.OutDir, path)
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return fmt.Errorf("Failed to make directory %v: %v", dir, err)
	}
	if err := os.WriteFile(path, data, 0666); err != nil {
		return fmt.Errorf("Failed to write file %v: %v", path, err)
	}
	return nil
}

func compressGZ(path string) error {
	d, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("Failed to read file %v: %v", path, err)
	}

	buf := bytes.Buffer{}
	zw := gzip.NewWriter(&buf)
	if _, err := zw.Write(d); err != nil {
		return fmt.Errorf("Failed to write zipped file %v", err)
	}
	if err := zw.Close(); err != nil {
		return fmt.Errorf("Failed to close zipped file %v", err)
	}
	path, err = filepath.Rel(Config.OutDir, path)
	if err != nil {
		return err
	}
	return writeFile(path+".gz", buf.Bytes())
}
