// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package world

import (
	"context"
	"fmt"
	"path/filepath"
	"regexp"
	"strings"
)

func (w *World) AddLicenseUrls() error {
	ctx := context.Background()
	urlFolderPattern := regexp.MustCompile(`.*\.com\/(.*)`)

	git, err := NewGit()
	if err != nil {
		return err
	}

	for _, p := range w.FilteredProjects {
		plus := "+"
		dir := p.Root

		url, err := git.GetURL(ctx, dir)
		if err != nil {
			return err
		}
		url = strings.ReplaceAll(url, "sso://turquoise-internal", "https://turquoise-internal.googlesource.com")

		if strings.HasPrefix(dir, "prebuilt") {
			url = "https://fuchsia-license.teams.x20web.corp.google.com/notices"
			plus = "%2B"
		}

		urlFolder := ""
		urlFolderList := urlFolderPattern.FindStringSubmatch(url)
		if len(urlFolderList) > 1 {
			urlFolder = urlFolderList[1]
		}

		if strings.Contains(url, "dart-pkg") {
			urlFolder = filepath.Join(urlFolder, "pub")
		}

		hash, err := git.GetCommitHash(ctx, dir)
		if err != nil {
			return err
		}

		for _, l := range p.LicenseFile {
			if l.Url != "" {
				continue
			}

			path := l.AbsPath
			if strings.Contains(path, Config.FuchsiaDir) {
				path, _ = filepath.Rel(Config.FuchsiaDir, path)
			}
			if strings.HasPrefix(path, urlFolder) {
				path, _ = filepath.Rel(urlFolder, path)
			}
			l.Url = fmt.Sprintf("%v/%v/%v/%v", url, plus, hash, path)

			if strings.HasPrefix(dir, "prebuilt") {
				plusVal(X20Licenses, l.Url)
			}
		}
	}
	return nil
}
