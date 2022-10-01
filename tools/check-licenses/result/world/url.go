// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package world

import (
	"fmt"
	"path/filepath"
	"strconv"
	"strings"
)

func (w *World) AddLicenseUrls() error {
	for _, p := range w.FilteredProjects {
		url := "https://fuchsia-license.teams.x20web.corp.google.com/notices"
		plus := "%2B"

		for _, l := range p.LicenseFile {
			path := l.AbsPath
			if strings.Contains(path, Config.FuchsiaDir) {
				path, _ = filepath.Rel(Config.FuchsiaDir, path)
			}

			// Segmented licenses
			for i, fd := range l.Data {
				strIndex := strconv.Itoa(i)
				fd.URL = fmt.Sprintf("%v/%v/%v/segments/%v", url, plus, path, strIndex)
			}

			if l.Url != "" {
				continue
			}

			l.Url = fmt.Sprintf("%v/%v/%v", url, plus, path)
		}
	}
	return nil
}
