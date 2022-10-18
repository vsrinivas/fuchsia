// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"fmt"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"io"
	"strings"
)

// extractCommentHeading1 separates out the first line if it starts with a single "#". If it does
// not start with a heading, the returned |heading| string will be empty. The remainder of the
// comment (or the full comment if there was no heading) is returned in |rest|.
func extractCommentHeading1(c []clangdoc.CommentInfo) (heading string, rest []clangdoc.CommentInfo) {
	if len(c) == 0 {
		// No content, fall through to returning nothing.
	} else if c[0].Kind == "TextComment" {
		// Note that the comments start with the space following the "///"
		if strings.HasPrefix(c[0].Text, " # ") {
			// Found heading.
			heading = c[0].Text[1:]
			rest = c[1:]
		} else {
			// Got to the end, there is no heading.
			rest = c
		}
	} else {
		// Need to recurse into the next level.
		innerHeading, innerRest := extractCommentHeading1(c[0].Children)
		heading = innerHeading

		// Need to make a deep copy because the Children is being modified.
		rest = make([]clangdoc.CommentInfo, len(c))
		copy(rest, c)
		rest[0].Children = innerRest
	}
	return
}

// Trims any leading markdown heading markers ("#") from the string.
func trimMarkdownHeadings(s string) string {
	return strings.TrimLeft(strings.TrimLeft(s, "#"), " ")
}

// See fixupLinks for a discussion on formatting.
func getLinkDest(index *Index, input []byte) []byte {
	// Strip everything after the "(" when doing a symbol lookup, but save it to put
	// back at the end.
	key := string(input)
	trailing := ""
	if paren := strings.IndexByte(key, byte('(')); paren > -1 {
		trailing = key[paren:]
		key = key[:paren]
	}

	link := "" // Resulting link dest.

	if foundFunc, ok := index.FunctionNames[key]; ok {
		link = functionLink(foundFunc)
	} else if foundRec, ok := index.RecordNames[key]; ok {
		link = recordLink(index, foundRec)
	} else if foundDef, ok := index.Defines[key]; ok {
		link = defineLink(*foundDef)
	} else if foundEnum, ok := index.Enums[key]; ok {
		link = enumLink(foundEnum)
	}

	if link != "" {
		// A markdown link would be:
		//   return []byte(fmt.Sprintf("[`%s%s`](%s)", key, trailing, link))
		// but we use HTML for the reasons described above fixupLinks().
		return []byte(fmt.Sprintf("<code><a href=\"%s\">%s</a>%s</code>",
			link, escapeHtml(key), escapeHtml(trailing)))
	}
	return []byte{}
}

// Converts any automatic links to known named items to actual markdown links. The input links we're
// looking for are surrounded by square brackets and have no parens after them (trailing parens
// indicate normal markdown links which we pass through unchanged).
//
//   - [LookUpThisNamedEntity] - our link, linkify it if there's a match in the index.
//   - [SomeFunction(foo, bar)] - parens inside the [] are ignored when doing name lookup.
//   - [something random](/docs/foo) - Markdown link, pass through unchanged.
//
// This does not handle [] links spread across multiple lines. The clang-doc output is line-based.
// We could put the lines back together to handle this case, but the contents we handle are normally
// single named C/C++ entities which can't have embedded whitespace anyway.
//
// We format our links as HTML <a href=...> which is handled well by docsite instead of Markdown.
// This is because we have more control over what is and isn't linked. In markdown you can't
// have a link inside code (because the [] are treated as literals). But if you want to have a
// function call link with parameters like:
//
//	[something_get_that(handle, output_value)]
//
// we have to either linkify the whole thing (looks weird) or format as two parts:
// [`text`](dest.md)`(params)` but devsite introduces a space between the two entities which
// looks bad.
func fixupLinks(index *Index, input []byte) []byte {
	// Looking for the pattern:
	//   <anything but "\"> "[" <anything>* <anything but "\"> "]" <anything but "(">
	output := make([]byte, 0, len(input))

	// Tracks the location of the most recent non-escaped '[' in both the input and output.
	openBracketInputIndex := -1
	openBracketOutputIndex := -1
	for i := 0; i < len(input); i++ {
		if input[i] == byte('\\') {
			// Escape, copy it and the next character without changing anything.
			output = append(output, input[i])
			i++
			output = append(output, input[i])
		} else if input[i] == byte('[') {
			// Got an open bracket. Save its location and continue copying.
			openBracketInputIndex = i
			openBracketOutputIndex = len(output)
			output = append(output, input[i])
		} else if input[i] == byte(']') && openBracketInputIndex >= 0 &&
			(i == len(input)-1 || input[i+1] != byte('(')) {
			// The ] must appear after a valid opening bracket and not be followed by
			// an open paren.
			linkText := getLinkDest(index, input[openBracketInputIndex+1:i])
			if len(linkText) > 0 {
				// Got a valid link, replace the whole bracketed text in
				// the output.
				output = output[:openBracketOutputIndex]
				output = append(output, linkText...)

				openBracketInputIndex = -1
				openBracketOutputIndex = -1
			} else {
				output = append(output, input[i])
			}
		} else {
			output = append(output, input[i])
		}
	}
	return output
}

// writeComment formats the given comments to the output. The heading depth is the number of "#" to
// add before any headings that appear in this text. It is used to "nest" the text in a new level.
func writeComment(index *Index, cs []clangdoc.CommentInfo, headingDepth int, f io.Writer) {
	for _, c := range cs {
		switch c.Kind {
		case "ParagraphComment":
			writeComment(index, c.Children, headingDepth, f)

			// Put a blank line after a paragraph.
			fmt.Fprintf(f, "\n")
		case "BlockCommandComment", "FullComment":
			// Just treat command comments as normal ones. The text will be in the
			// children.
			writeComment(index, c.Children, headingDepth, f)
		case "TextComment":
			// Strip one leading space if there is one.
			var line string
			if len(c.Text) > 0 && c.Text[0] == ' ' {
				line = c.Text[1:]
			} else {
				line = c.Text
			}

			// If it's a markdown heading, knock it down into our hierarchy.
			if len(line) > 0 && line[0] == '#' {
				fmt.Fprintf(f, "%s", headingMarkerAtLevel(headingDepth))
			}
			f.Write(fixupLinks(index, []byte(line)))
			fmt.Fprintf(f, "\n")
		}
	}
}
