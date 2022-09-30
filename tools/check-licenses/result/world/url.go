// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package world

import (
	"fmt"
	"strconv"
)

func (w *World) AddLicenseUrls() error {
	for _, p := range w.Projects {
		url := "https://fuchsia-license.teams.x20web.corp.google.com/check-licenses"
		version := Config.BuildInfoVersion
		target := fmt.Sprintf("%v.%v", Config.BuildInfoProduct, Config.BuildInfoBoard)

		for _, l := range p.LicenseFile {
			path := fmt.Sprintf("%v/%v/license/matches/%v", version, target, l.RelPath)

			// Segmented licenses
			for i, fd := range l.Data {
				if fd.URL != "" {
					continue
				}

				strIndex := strconv.Itoa(i)
				suffix := fmt.Sprintf("segments/%v", strIndex)
				fd.URL = fmt.Sprintf("%v/%v/%v", url, path, suffix)
			}

			if l.URL != "" {
				continue
			}

			l.URL = fmt.Sprintf("%v/%v/%v", url, version, path)
		}
	}
	return nil
}
