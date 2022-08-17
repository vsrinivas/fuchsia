// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codelinks

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/staticanalysis"
)

// rules is a list of bad link formats that should not be included in source
// code.
var rules = []struct {
	// A pattern that matches URLs that should be considered invalid.
	pattern *regexp.Regexp
	// An optional template for constructing replacement text for a matched URL.
	// The template may use ${name} variables which will be replaced with
	// (?P<name>...) submatches when constructing the suggested replacement
	// text.
	replacementTemplate string
	// A message that will be included in a comment on the code change.
	message string
}{
	{
		pattern:             regexp.MustCompile(`(https?://)?fuchsia.googlesource.com/fuchsia/\+/(refs/heads/)?\w+/docs/(?P<path>\S+)\.md`),
		replacementTemplate: "https://fuchsia.dev/fuchsia-src/${path}",
		message:             "Documentation links should point to fuchsia.dev rather than fuchsia.googlesource.com.",
	},
}

type analyzer struct {
	checkoutDir string
}

// New returns an analyzer that checks code for malformatted hyperlinks.
func New(checkoutDir string) staticanalysis.Analyzer {
	return &analyzer{checkoutDir: checkoutDir}
}

func (a *analyzer) Analyze(_ context.Context, path string) ([]*staticanalysis.Finding, error) {
	if strings.Split(path, "/")[0] == "docs" {
		// TODO(olivernewman): Files under //docs should generally reference
		// other documentation files by path (e.g. "//docs/foo/bar.md") rather
		// than URL, with some exceptions for reference docs.
		return nil, nil
	}

	b, err := os.ReadFile(filepath.Join(a.checkoutDir, path))
	if err != nil {
		return nil, err
	}

	var findings []*staticanalysis.Finding
	for i, line := range strings.Split(string(b), "\n") {
		for _, rule := range rules {
			if rule.message == "" {
				return nil, fmt.Errorf("code link rule must have a 'message' set: %#+v", rule)
			}
			matches := rule.pattern.FindAllStringSubmatchIndex(line, -1)
			for _, match := range matches {
				finding := &staticanalysis.Finding{
					Category:  "BadCodeLink",
					Message:   rule.message,
					Path:      path,
					StartLine: i + 1,
					EndLine:   i + 1,
					StartChar: match[0],
					EndChar:   match[1],
				}
				if rule.replacementTemplate != "" {
					replacementBytes := rule.pattern.ExpandString(nil, rule.replacementTemplate, line, match)
					finding.Suggestions = []staticanalysis.Suggestion{
						{
							Replacements: []staticanalysis.Replacement{
								{
									Path:        path,
									Replacement: string(replacementBytes),
									StartLine:   i + 1,
									EndLine:     i + 1,
									StartChar:   match[0],
									EndChar:     match[1],
								},
							},
						},
					}
				}
				findings = append(findings, finding)
			}
		}
	}

	return findings, nil
}
