// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rfcmeta

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	"gopkg.in/yaml.v2"

	"go.fuchsia.dev/fuchsia/tools/staticanalysis"
)

const (
	rfcsDir      = "docs/contribute/governance/rfcs"
	tocPath      = "docs/contribute/governance/rfcs/_toc.yaml"
	rfcIndexPath = "docs/contribute/governance/rfcs/_rfcs.yaml"
	areasPath    = "docs/contribute/governance/rfcs/_areas.yaml"
)

type analyzer struct {
	checkoutDir string
}

// New returns an analyzer that checks the filenames and metadata for RFCs.
func New(checkoutDir string) staticanalysis.Analyzer {
	return &analyzer{checkoutDir: checkoutDir}
}

func (a *analyzer) Analyze(_ context.Context, path string) ([]*staticanalysis.Finding, error) {
	// Ignore files that aren't a direct child of the RFC directory.
	if matched, err := filepath.Match(filepath.Join(rfcsDir, "*"), path); err != nil {
		return nil, err
	} else if !matched {
		return nil, nil
	}

	if path == tocPath {
		return a.analyzeToc()
	}

	if path == rfcIndexPath {
		return a.analyzeRfcIndex()
	}

	if rfcId := parseRfcPath(path); rfcId != "" {
		return a.analyzeRfcFile(path, rfcId)
	}

	return nil, nil
}

var reRfcFilename = regexp.MustCompile(`^(....)_.*\.md$`)

// parseRfcPath returns the RFC ID from a path that looks like an RFC, or the
// empty string otherwise.
func parseRfcPath(path string) string {
	match := reRfcFilename.FindStringSubmatch(filepath.Base(path))
	if match == nil {
		return ""
	}

	// Special case for "best_practices.md".
	if match[1] == "best" {
		return ""
	}
	return match[1]
}

// toc represents a subset of the schema of _toc.yaml.
type toc struct {
	Entries []*tocEntry `yaml:"toc"`
}

// tocEntry is one entry in _toc.yaml.
type tocEntry struct {
	Title   string      `yaml:"title"`
	Path    string      `yaml:"path"`
	Section []*tocEntry `yaml:"section"`
}

// analyzeToc returns findings from _toc.yaml.
func (a *analyzer) analyzeToc() ([]*staticanalysis.Finding, error) {
	file, err := os.ReadFile(filepath.Join(a.checkoutDir, tocPath))
	if err != nil {
		return nil, err
	}

	var toc toc
	if err := yaml.Unmarshal(file, &toc); err != nil {
		return []*staticanalysis.Finding{
			{
				Category: "rfcmeta/toc/failed_to_parse",
				Message:  fmt.Sprintf("Failed to parse yaml: %v", err),
				Path:     tocPath,
			},
		}, nil
	}

	var findings []*staticanalysis.Finding
	for _, entry := range flattenTocEntries(toc.Entries) {
		if entry.Title == "" {
			findings = append(findings, &staticanalysis.Finding{
				Category: "rfcmeta/toc/missing_name",
				Message:  fmt.Sprintf("An entry is missing the `title` field"),
				Path:     tocPath,
				// Don't include a line number, because searching for the empty
				// string isn't going to yield helpful results.
			})
			continue
		}

		// Ignore entries that don't look like RFCs
		titleRfcId := parseRfcTitle(entry.Title)
		if titleRfcId == "" {
			continue
		}

		// Attach findings to the line with the title, since we know it's not empty.
		lineNo := findLineContaining(file, entry.Title)

		// Ensure the path matches the RFC ID.
		expectedPathPrefix := filepath.Join("/", rfcsDir, titleRfcId+"_")
		if !strings.HasPrefix(entry.Path, expectedPathPrefix) {
			findings = append(findings, &staticanalysis.Finding{
				Category: "rfcmeta/toc/unexpected_path",
				Message: fmt.Sprintf("path for %q should begin with %q; found %q",
					entry.Title, expectedPathPrefix, entry.Path),
				Path:      tocPath,
				StartLine: lineNo,
				EndLine:   lineNo,
			})
		}

		// Ensure the path actually exists.
		if _, err := os.Stat(filepath.Join(a.checkoutDir, entry.Path)); errors.Is(err, os.ErrNotExist) {
			findings = append(findings, &staticanalysis.Finding{
				Category:  "rfcmeta/toc/file_not_found",
				Message:   fmt.Sprintf("File %q doesn't exist", entry.Path),
				Path:      tocPath,
				StartLine: lineNo,
				EndLine:   lineNo,
			})

		} else if err != nil {
			return findings, err
		}
	}
	return findings, nil
}

// rfcIndexEntry represents a subset of the schema for entries in _rfcs.yaml.
type rfcIndexEntry struct {
	Name  string   `yaml:"name"`
	Title string   `yaml:"title"`
	File  string   `yaml:"file"`
	Areas []string `yaml:"area"`
}

// analyzeRfcIndex returns findings from _rfcs.yaml.
func (a *analyzer) analyzeRfcIndex() ([]*staticanalysis.Finding, error) {
	file, err := os.ReadFile(filepath.Join(a.checkoutDir, rfcIndexPath))
	if err != nil {
		return nil, err
	}

	var index []*rfcIndexEntry
	if err := yaml.Unmarshal(file, &index); err != nil {
		return []*staticanalysis.Finding{
			{
				Category: "rfcmeta/index/failed_to_parse",
				Message:  fmt.Sprintf("Failed to parse yaml: %v", err),
				Path:     rfcIndexPath,
			},
		}, nil
	}

	var findings []*staticanalysis.Finding
	for _, rfc := range index {
		if rfc.Name == "" {
			findings = append(findings, &staticanalysis.Finding{
				Category: "rfcmeta/index/missing_name",
				Message:  fmt.Sprintf("An entry is missing the `name` field"),
				Path:     rfcIndexPath,
				// Don't include a line number, because searching for the empty
				// string isn't going to yield helpful results.
			})
			continue
		}

		rfcId := parseRfcTitle(rfc.Name)

		// Display findings on the line with the name, because we at least know
		// it's non-empty.
		lineNo := findLineContaining(file, rfc.Name)
		if rfcId == "" {
			findings = append(findings, &staticanalysis.Finding{
				Category: "rfcmeta/index/invalid_name",
				Message: fmt.Sprintf(
					"RFC name %q should look like \"RFC-1234\"", rfc.Title),
				Path:      rfcIndexPath,
				StartLine: lineNo,
				EndLine:   lineNo,
			})
			continue
		}

		// RFC-0000 is the template, so it's special. Skip the other checks.
		if rfcId == "0000" {
			continue
		}

		// Check that the "area" field is not empty.
		if len(rfc.Areas) == 0 {
			findings = append(findings, &staticanalysis.Finding{
				Category: "rfcmeta/index/missing_area",
				Message: fmt.Sprintf("Include an 'area' for this RFC. Options are listed in //%s",
					areasPath),
				Path:      rfcIndexPath,
				StartLine: lineNo,
				EndLine:   lineNo,
			})
		}

		// Check that the "area" field refers to known areas.
		knownAreas := a.loadAreas()
		for _, area := range rfc.Areas {
			if !knownAreas[area] {
				findings = append(findings, &staticanalysis.Finding{
					Category: "rfcmeta/index/unknown_area",
					Message: fmt.Sprintf("area %q is not listed in //%s",
						area, areasPath),
					Path:      rfcIndexPath,
					StartLine: lineNo,
					EndLine:   lineNo,
				})
			}
		}

		// Check that the path looks right.
		if !strings.HasPrefix(rfc.File, rfcId+"_") {
			findings = append(findings, &staticanalysis.Finding{
				Category: "rfcmeta/index/unexpected_path",
				Message: fmt.Sprintf("path for %q should begin with %q; found %q",
					rfc.Name, rfcId+"_", rfc.File),
				Path:      rfcIndexPath,
				StartLine: lineNo,
				EndLine:   lineNo,
			})
		}

		// ... and that there's a real file there.
		if _, err := os.Stat(filepath.Join(a.checkoutDir, rfcsDir, rfc.File)); errors.Is(err, os.ErrNotExist) {
			findings = append(findings, &staticanalysis.Finding{
				Category: "rfcmeta/index/file_not_found",
				Message: fmt.Sprintf("file %q does not exist",
					filepath.Join(rfcsDir, rfc.File)),
				Path:      rfcIndexPath,
				StartLine: lineNo,
				EndLine:   lineNo,
			})
		} else if err != nil {
			return findings, err
		}
	}
	return findings, nil
}

// analyzeRfcIndex returns findings from the markdown file itself for an RFC.
// `rfcId` should be the RFC's 4-character ID, parsed out of `path`.
func (a *analyzer) analyzeRfcFile(path string, rfcId string) ([]*staticanalysis.Finding, error) {
	var findings []*staticanalysis.Finding

	// Check if the `rfcId` looks like a placeholder, i.e., it's 4 copies of the
	// same character.
	//
	// If you're here because we got to RFC-1111, greetings from the past!
	if rfcId[0] == rfcId[1] && rfcId[1] == rfcId[2] && rfcId[2] == rfcId[3] {
		finding := &staticanalysis.Finding{
			Category: "rfcmeta/file/placeholder_id",
			Message: fmt.Sprintf(
				"RFC filename begins with %q. Replace it with the RFC number before submitting.",
				rfcId),
			Path: path,
		}
		findings = append(findings, finding)
	}

	// Check if it's in the table of contents.
	if !a.tocContainsPath(path) {
		findings = append(findings, &staticanalysis.Finding{
			Category: "rfcmeta/file/not_in_toc",
			Message:  "No matching entry in _toc.yaml",
			Path:     path,
		})
	}

	// Check if it's in the RFC index.
	if !a.rfcIndexContainsPath(path) {
		findings = append(findings, &staticanalysis.Finding{
			Category: "rfcmeta/file/not_in_index",
			Message:  "RFC is not listed in _rfcs.yaml",
			Path:     path,
		})
	}

	loadedFile, err := os.ReadFile(filepath.Join(a.checkoutDir, path))
	if err != nil {
		return findings, err
	}
	file := strings.Split(string(loadedFile), "\n")

	// Look for the `set rfcid =` tag.
	findings = append(findings, analyzeSetRfcIdTag(path, rfcId, file)...)

	return findings, nil
}

// tocContainsPath returns true if an entry corresponding to `path` was found in
// the table of contents. Any error encountered while reading or parsing
// `_toc.yaml` counts as "not finding the path", and this function will return
// false.
func (a *analyzer) tocContainsPath(path string) bool {
	b, err := os.ReadFile(filepath.Join(a.checkoutDir, tocPath))
	if err != nil {
		return false
	}

	var toc toc
	if err := yaml.Unmarshal(b, &toc); err != nil {
		return false
	}

	for _, entry := range flattenTocEntries(toc.Entries) {
		if entry.Path == filepath.Join("/", path) {
			return true
		}
	}
	return false
}

// rfcIndexContainsPath returns true if an entry corresponding to `path` was
// found in the RFC metadata index. Any error encountered while reading or
// parsing `_rfcs.yaml` counts as "not finding the path", and this function will
// return false.
func (a *analyzer) rfcIndexContainsPath(path string) bool {
	b, err := os.ReadFile(filepath.Join(a.checkoutDir, rfcIndexPath))
	if err != nil {
		return false
	}

	rfcIndex := []*rfcIndexEntry{}
	if err = yaml.Unmarshal(b, &rfcIndex); err != nil {
		return false
	}

	for _, r := range rfcIndex {
		if r.File == filepath.Base(path) {
			return true
		}
	}
	return false
}

// loadAreas returns a set of strings representing the areas in
// rfcs/_areas.yaml. If this function encounters any problems while reading or
// parsing `_areas.yaml` it will return nil (i.e., an empty set).
func (a *analyzer) loadAreas() map[string]bool {
	b, err := os.ReadFile(filepath.Join(a.checkoutDir, areasPath))
	if err != nil {
		return nil
	}

	areasList := []string{}
	if err = yaml.Unmarshal(b, &areasList); err != nil {
		return nil
	}

	areasMap := make(map[string]bool)
	for _, area := range areasList {
		areasMap[area] = true
	}
	return areasMap
}

var reSetRfcIdTag = regexp.MustCompile(`(^.*set *rfcid *= *"RFC-)([^"]*)(".*$)`)

// analyzeSetRfcIdTag looks for a jinja tag like {% set rfcid = "RFC-1234" %} in
// the given file. Findings will be returned if the tag is missing, or if it
// doesn't have the correct RFC ID.
func analyzeSetRfcIdTag(path string, rfcId string, file []string) []*staticanalysis.Finding {
	for i, line := range file {
		match := reSetRfcIdTag.FindStringSubmatch(line)
		if match == nil {
			continue
		}

		if match[2] == rfcId {
			// The tag looks good.
			return nil
		} else {
			return []*staticanalysis.Finding{{
				Category: "rfcmeta/file/rfcid_mismatch",
				Message:  fmt.Sprintf("Filename has RFC ID %q, but the rfcid tag has ID %q", rfcId, match[2]),
				Path:     path,

				StartLine: i + 1,
				EndLine:   i + 1,
				StartChar: 0,
				EndChar:   len(line),

				Suggestions: []staticanalysis.Suggestion{{
					Description: "Fix the tag to match the filename.",
					Replacements: []staticanalysis.Replacement{
						{
							Path:        path,
							Replacement: match[1] + rfcId + match[3],
							StartLine:   i + 1,
							EndLine:     i + 1,
							StartChar:   0,
							EndChar:     len(line),
						},
					}}}}}
		}
	}

	// If we made it to the end without finding the tag, complain.
	return []*staticanalysis.Finding{{
		Category: "rfcmeta/file/rfcid_tag_not_found",
		Message:  "No `{% set rfcid = \"RFC-" + rfcId + "\" %}` tag found.",
		Path:     path,
	}}
}

var reRfcTitle = regexp.MustCompile("^RFC-(....)")

// parseRfcTitle pulls the RFC ID out of a string like "RFC-1234: ...". If `s`
// doesn't look like an RFC title, it returns the empty string.
func parseRfcTitle(s string) string {
	match := reRfcTitle.FindStringSubmatch(s)
	if match == nil {
		return ""
	}
	return match[1]
}

// findLineContaining returns the 1-indexed line number of the first line
// containing `substring` in `file`. If no such line is found, it returns 0.
//
// Use of this function is mostly a hack. The yaml.v2 module discards line
// number information when parsing YAML, so we use this function to guess as to
// where the errors might be in the file. yaml.v3 supports returning an AST, so
// the "correct" thing to do would be to upgrade yaml.v2 to yaml.v3 and get the
// line numbers from the parser.
func findLineContaining(file []byte, substr string) int {
	for i, line := range bytes.Split(file, []byte{'\n'}) {
		if strings.Contains(string(line), substr) {
			return i + 1
		}
	}
	return 0
}

// flattenTocEntries flattens the table of contents by removing any nesting with
// "sections", and returning a list of the "leaf" entries.
func flattenTocEntries(toc []*tocEntry) []*tocEntry {
	var res []*tocEntry
	for _, entry := range toc {
		if len(entry.Section) != 0 {
			res = append(res, flattenTocEntries(entry.Section)...)
		} else {
			res = append(res, entry)
		}
	}
	return res
}
