// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"log"
	"os"
	"strings"
	"unicode"

	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
)

type LineClass int

const (
	LineClassBlank LineClass = iota
	LineClassComment
	LineClassPreproc
	LineClassCode
)

type Define struct {
	Location    clangdoc.Location
	Name        string
	ParamString string // For "#define FOO(a, b) ..." this is "(a, b)".
	Value       string
	Description []clangdoc.CommentInfo
}

// Interface for sorting a define list by declaration location.
type defineByLocation []*Define

func (f defineByLocation) Len() int {
	return len(f)
}
func (f defineByLocation) Swap(i, j int) {
	f[i], f[j] = f[j], f[i]
}
func (f defineByLocation) Less(i, j int) bool {
	// This assumes the file names are the same (since we're normally processing by file).
	return f[i].Location.LineNumber < f[j].Location.LineNumber
}

type HeaderValues struct {
	// The per-header-file comment.
	Description []clangdoc.CommentInfo

	// Defines set in this header, in order they appear.
	Defines []*Define

	// The classification of each line in the header.
	Classes []LineClass
}

// Returns a bool indicating if this is preprocessor line, and if so also returns the contents of
// the line (after the "#").
func isPreprocLine(t string) (isPreproc bool, contents string) {
	isPreproc = len(t) > 0 && t[0] == '#'
	if isPreproc {
		contents = t[1:]
	}
	return
}

func isCommentLine(t string) (isComment bool, isDocstring bool, contents string) {
	isComment = len(t) >= 2 && t[0] == '/' && t[1] == '/'
	isDocstring = isComment && len(t) >= 3 && t[2] == '/'
	if isDocstring {
		contents = t[3:]
	} else if isComment {
		contents = t[2:]
	}
	return
}

// This doesn't generate comments exactly like clang-doc, but rather better suited to our uses, (no
// paragraphs, including blank lines).
func commentsToDescription(c []string) []clangdoc.CommentInfo {
	if len(c) == 0 {
		return nil
	}
	full := clangdoc.CommentInfo{Kind: "FullComment"}
	for _, line := range c {
		full.Children = append(full.Children, clangdoc.CommentInfo{
			Kind: "TextComment",
			Text: line,
		})
	}
	return []clangdoc.CommentInfo{full}
}

// The |contents| is the stuff after the "#". This assumes only ASCII spaces are valid.
func parsePreprocContents(contents string, comments []string, loc clangdoc.Location, vals *HeaderValues) {
	i := 0

	skipSpace := func() {
		// Assumes only ASCII whitespace (converts char to rune).
		for i < len(contents) && unicode.IsSpace(rune(contents[i])) {
			i++
		}
	}

	skipSpace() // Skip spaces between the "#" and the directive.

	const define = "define"
	if !strings.HasPrefix(contents[i:], define) {
		return // Doesn't start with "define".
	}
	i += len(define)

	skipSpace()

	// What follows is the name. Count all A-Z, a-z, 0-9, '_'. Technically it can't start with a
	// digit but that level of validity doesn't matter here.
	nameBegin := i
	isNameChar := func(c rune) bool {
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'
	}
	for i < len(contents) && isNameChar(rune(contents[i])) {
		i++
	}
	name := contents[nameBegin:i]

	// Macro parameters, if any, follow as a paren immediately after the name.
	paramBegin := i
	if i < len(contents) && contents[i] == '(' {
		// Got a parameter list, extract it. This only extracts the first line for multiline param strings.
		//
		// We don't bother to extract the individual parameters, just the string until the
		// closing paren. This should handle everything except if there's a paren inside a
		// comment inside the parameter list.
		i++ // Skip over opening paren.
		for i < len(contents) {
			if rune(contents[i]) == ')' {
				i++ // Skip over paren.
				break
			}
			i++
		}
	}
	paramString := ""
	if paramBegin < i {
		paramString = contents[paramBegin:i]
	}

	// Everything else to the end-of-the-line is the value.
	value := strings.TrimSpace(contents[i:])
	if len(value) == 0 {
		return // Ignore #defines with no value, assume they don't need docs.
	}

	desc := commentsToDescription(comments)
	if commentContains(desc, NoDocTag) {
		return // Ignore #defines with the "$nodoc" annotation in the comment.
	}

	vals.Defines = append(vals.Defines, &Define{
		Location:    loc,
		Name:        name,
		ParamString: paramString,
		Value:       value,
		Description: commentsToDescription(comments),
	})

	// TODO look for end-of-line comments here and use that if the |comments| are empty.
}

// The header contents are passed in as |h|, the filename is used only for the location reporting.
func ParseHeader(h string, filename string) (vals HeaderValues) {
	// Holds the lines of the current set of comment lines. This is reset when we get a
	// non-comment line.
	comments := make([]string, 0, 20)
	commentsAreDocstring := false

	foundHeaderDocstring := false

	// Set when there is a nonempty line that isn't handled by the preprocessor (# and //).
	hasNonPreprocLine := false

	lines := strings.Split(h, "\n")
	vals.Classes = make([]LineClass, len(lines), len(lines))

	for i, t := range lines {
		t := strings.TrimSpace(t)

		if len(t) == 0 && commentsAreDocstring && len(comments) > 0 && !foundHeaderDocstring && !hasNonPreprocLine {
			// Got an empty line at the end of a docstring comment where we can accept
			// the header docstring. This indicates the end of the header docstring,
			// consume it and clear the state.
			vals.Description = commentsToDescription(comments)
			comments = comments[0:0]
			foundHeaderDocstring = true
			continue
		}

		if len(t) == 0 {
			comments = comments[0:0]
			continue
		}

		isCommentLine, isDocstring, contents := isCommentLine(t)
		if isCommentLine {
			if isDocstring != commentsAreDocstring {
				// Transitioned between docstring and not, reset accumulator.
				comments = comments[0:0]
			}

			// Accumulate the current comment line.
			commentsAreDocstring = isDocstring
			comments = append(comments, contents)
			vals.Classes[i] = LineClassComment
			continue
		}

		isPreproc, contents := isPreprocLine(t)
		if isPreproc {
			loc := clangdoc.Location{LineNumber: i + 1, Filename: filename}
			parsePreprocContents(contents, comments, loc, &vals)
			comments = comments[0:0]
			vals.Classes[i] = LineClassPreproc
			continue
		}

		// Hit a non-comment line we don't care about, reset the comment state.
		comments = comments[0:0]
		vals.Classes[i] = LineClassCode
		hasNonPreprocLine = true
	}
	return
}

func ReadHeader(name string) HeaderValues {
	content, err := os.ReadFile(name)
	if err != nil {
		log.Fatal(err)
	}
	return ParseHeader(string(content), name)
}
