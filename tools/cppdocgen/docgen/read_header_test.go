// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"reflect"
	"testing"
)

// This makes some assumptions about the format of the comment hierarchy that is allowed to change
// over time (there may be "paragraph" comments as a middle hierarchy, for example).
func assertDocstringIs(t *testing.T, actual []clangdoc.CommentInfo, expected []string) {
	if len(expected) == 0 {
		if len(actual) > 0 {
			t.Errorf("Expecting empty docstring.")
		}
		return
	}

	// Outer one is always the full comment.
	if len(actual) != 1 {
		t.Errorf("Expecting a single comment, got %d", len(actual))
		return
	}
	if actual[0].Kind != "FullComment" {
		t.Errorf("Expecting a full comment, got '%s'", actual[0].Kind)
		return
	}

	children := actual[0].Children
	if len(children) != len(expected) {
		t.Errorf("Expecting %d lines of header docstring, got %d\n",
			len(expected), len(children))
		return
	}
	for i := 0; i < len(expected); i++ {
		if children[i].Kind != "TextComment" {
			t.Errorf("Expecting 'TextComment', got '%s'\n", children[i].Kind)
		}
		if children[i].Text != expected[i] {
			t.Errorf("Expecting for line %d of docstring:\n  '%s'\nGot:\n  '%s'\n",
				i, expected[i], children[i].Text)
		}
	}
}

func assertDefinesAre(t *testing.T, actual []*Define, expected []*Define) {
	if !reflect.DeepEqual(actual, expected) {
		t.Errorf("For defines, expected\n  %v\nGot\n  %v\n", expected, actual)
		return
	}
}

// This also tests line classifications.
func TestParseHeaderWithHeaderDocstring(t *testing.T) {
	contents := `// Copyright blah blah
// Second line of copyright

#ifdef FOO
#define FOO

/// This is the header docstring.
///
/// It is nice

// Not the header docstring, random thing to delete.

// Fromulator max description.
//
// Woot
#define FROMULATOR_MAX 512

/// Complicated multiline define.
#define FROMULATOR_GET_MIN(a, b) {\
  DoTheThing(a, b) \
}

int GetThing();

#endif
`
	vals := ParseHeader(contents, "filename.h")
	assertDocstringIs(t, vals.Description, []string{
		" This is the header docstring.",
		"",
		" It is nice",
	})

	expectedDefines := []*Define{
		{Location: clangdoc.Location{LineNumber: 16, Filename: "filename.h"},
			Name:        "FROMULATOR_MAX",
			ParamString: "",
			Value:       "512",
			Description: commentsToDescription([]string{" Fromulator max description.", "", " Woot"}),
		},
		{Location: clangdoc.Location{LineNumber: 19, Filename: "filename.h"},
			Name:        "FROMULATOR_GET_MIN",
			ParamString: "(a, b)",
			Value:       "{\\",
			Description: commentsToDescription([]string{" Complicated multiline define."}),
		},
	}
	assertDefinesAre(t, vals.Defines, expectedDefines)

	// Corresponds to the classification of each line in the code block above.
	expectedClasses := [...]LineClass{
		LineClassComment,
		LineClassComment,
		LineClassBlank,
		LineClassPreproc,
		LineClassPreproc,
		LineClassBlank,
		LineClassComment,
		LineClassComment,
		LineClassComment,
		LineClassBlank,
		LineClassComment,
		LineClassBlank,
		LineClassComment,
		LineClassComment,
		LineClassComment,
		LineClassPreproc,
		LineClassBlank,
		LineClassComment,
		LineClassPreproc,
		LineClassCode, // This should be preproc but we don't handle multiline defines yet.
		LineClassCode, // This should be preproc but we don't handle multiline defines yet.
		LineClassBlank,
		LineClassCode,
		LineClassBlank,
		LineClassPreproc,
		LineClassBlank}

	if len(expectedClasses) != len(vals.Classes) {
		t.Errorf("Expecting %d lines of header line classes, got %d\n",
			len(expectedClasses), len(vals.Classes))
	}
	for i := range expectedClasses {
		if expectedClasses[i] != vals.Classes[i] {
			t.Errorf("Expected line index %d to be class %d, got %d\n",
				i, expectedClasses[i], vals.Classes[i])
		}
	}
}

func TestParseHeaderWithNonDocstring(t *testing.T) {
	contents := `// Copyright blah blah
// Second line of copyright

#ifdef FOO
#define FOO

/// Not the header docstring
int SomeFunc();

/// This is not the header docstring either.
///
/// It is nice
`
	vals := ParseHeader(contents, "filename.h")
	if len(vals.Description) != 0 {
		t.Errorf("Expected no docstring.")
	}
}

func TestParseHeaderDefines(t *testing.T) {
	contents := `// Copyright blah blah
// Second line of copyright

#define FOO

#define LAZ   a

/// Some docs
///
/// For the define
#define BAR (a)

// Currently this just gets the params from the first line.
 #  define    	TAZ( a, b )   \
           blah


// This define should be skipped because of: $nodoc
#define THE_ONE_THAT_SHOULD_NOT_BE_DOCUMENTED true
`
	vals := ParseHeader(contents, "filename.h")
	expectedDefines := []*Define{
		// Note that "FOO" is not listed because it has no value.
		{Location: clangdoc.Location{LineNumber: 6, Filename: "filename.h"},
			Name:        "LAZ",
			ParamString: "",
			Value:       "a",
		},
		{Location: clangdoc.Location{LineNumber: 11, Filename: "filename.h"},
			Name:        "BAR",
			ParamString: "",
			Value:       "(a)",
			Description: commentsToDescription([]string{" Some docs", "", " For the define"}),
		},
		{Location: clangdoc.Location{LineNumber: 14, Filename: "filename.h"},
			Name:        "TAZ",
			ParamString: "( a, b )",
			Value:       "\\",
			Description: commentsToDescription([]string{" Currently this just gets the params from the first line."}),
		},
	}
	assertDefinesAre(t, vals.Defines, expectedDefines)

}
