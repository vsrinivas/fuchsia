// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package result

import (
	"fmt"
	"regexp"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/filetree"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/license"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

func RunChecks() error {
	if err := AllFuchsiaAuthorSourceFilesMustHaveCopyrightHeaders(); err != nil {
		return err
	}
	if err := AllLicenseTextsMustBeRecognized(); err != nil {
		// Disable the license checker for f8 only.
		return nil
	}
	if err := AllLicensePatternUsagesMustBeApproved(); err != nil {
		// TODO: Enable this check after license pattern review.
		return nil
	}
	if err := AllProjectsMustHaveALicense(); err != nil {
		// TODO: Enable this check after all projects have been addressed.
		return nil
	}
	if err := AllFilesAndFoldersMustBeIncludedInAProject(); err != nil {
		// TODO: Enable this check after all projects have been addressed.
		return nil
	}
	if err := AllReadmeFuchsiaFilesMustBeFormattedCorrectly(); err != nil {
		// TODO: Enable this check after all projects have been addressed.
		return nil
	}
	return nil
}

// =============================================================================

func AllFuchsiaAuthorSourceFilesMustHaveCopyrightHeaders() error {
	var b strings.Builder
	b.WriteString("All source files owned by The Fuchsia Authors must contain a copyright header.\n")
	b.WriteString("The following files have missing or incorrectly worded copyright header information:\n\n")

	var fuchsia *project.Project
	for _, p := range project.AllProjects {
		if p.Root == "." {
			fuchsia = p
			break
		}
	}

	if fuchsia == nil {
		return fmt.Errorf("Couldn't find Fuchsia project to verify this check!!\n")
	}
	count := 0
	for _, f := range fuchsia.SearchableFiles {
		if len(f.Data) == 0 {
			return fmt.Errorf("Found a file that hasn't been parsed yet?? %v\n", f.Path)
		}
	OUTER:
		for _, fd := range f.Data {
			for _, p := range license.AllCopyrightPatterns {
				if _, ok := p.PreviousMatches[fd.Hash()]; ok {
					continue OUTER
				}
			}
			b.WriteString(fmt.Sprintf("-> %v\n", fd.FilePath))
			count = count + 1
		}
	}
	b.WriteString(fmt.Sprintf("\nPlease add the standard Fuchsia copyright header info to the above %v files.\n", count))
	if count > 0 {
		return fmt.Errorf(b.String())
	}
	return nil
}

func AllLicenseTextsMustBeRecognized() error {
	if len(license.Unrecognized.Matches) > 0 {
		var b strings.Builder
		b.WriteString("Found unrecognized license texts - please add the relevant license pattern(s) to //tools/check-licenses/license/patterns/* and have it(them) reviewed by the OSRB team:\n\n")
		for _, m := range license.Unrecognized.Matches {
			b.WriteString(fmt.Sprintf("-> Line %v of %v\n", m.LineNumber, m.FilePath))
			b.WriteString(fmt.Sprintf("\n%v\n\n", string(m.Data)))
		}
		return fmt.Errorf(b.String())
	}
	return nil
}

func AllLicensePatternUsagesMustBeApproved() error {
	var b strings.Builder
OUTER:
	for _, sr := range license.AllSearchResults {
		filepath := sr.LicenseData.FilePath
		allowlist := sr.Pattern.AllowList
		for _, entry := range allowlist {
			re, err := regexp.Compile("(" + entry + ")")
			if err != nil {
				return err
			}
			if m := re.Find([]byte(filepath)); m != nil {
				continue OUTER
			}
		}
		b.WriteString(fmt.Sprintf("File %v was not approved to use license pattern %v\n", filepath, sr.Pattern.Name))
	}

	result := b.String()
	if len(result) > 0 {
		return fmt.Errorf("Encountered license texts that were not approved for usage:\n%v", result)
	}
	return nil
}

func AllProjectsMustHaveALicense() error {
	var b strings.Builder
	b.WriteString("All projects should include relevant license information, and a README.fuchsia file pointing to the file.\n")
	b.WriteString("The following projects were found without any license information:\n\n")
	count := 0
	for _, p := range project.AllProjects {
		if len(p.LicenseFile) == 0 {
			count = count + 1
			b.WriteString(fmt.Sprintf("-> %v (README.fuchsia file: %v)\n", p.Root, p.ReadmePath))
		}
	}
	b.WriteString("\nPlease add a LICENSE file to the above projects, and point to them in the associated README.fuchsia file.\n")
	if count > 0 {
		return fmt.Errorf(b.String())
	}
	return nil
}

func AllFilesAndFoldersMustBeIncludedInAProject() error {
	var b strings.Builder
	var recurse func(*filetree.FileTree)
	count := 0

	b.WriteString("All files and folders must have proper license attribution.\n")
	b.WriteString("This means a license file needs to accompany all first and third party projects,\n")
	b.WriteString("and a README.fuchsia file must exist and specify where the license file lives.\n")
	b.WriteString("The following directories are not included in a project:\n\n")
	recurse = func(ft *filetree.FileTree) {
		if ft.Project == nil {
			b.WriteString(fmt.Sprintf("-> %v\n", ft.Path))
			count = count + 1
			return
		}
		for _, child := range ft.Children {
			recurse(child)
		}
	}
	b.WriteString("\nPlease add a LICENSE file to the above projects, and point to them in an associated README.fuchsia file.\n")

	recurse(filetree.RootFileTree)
	if count > 0 {
		return fmt.Errorf(b.String())
	}
	return nil
}

func AllReadmeFuchsiaFilesMustBeFormattedCorrectly() error {
	//TODO
	return nil
}
