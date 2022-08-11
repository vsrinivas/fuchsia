// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package docgen

import (
	"fmt"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"io"
	"sort"
	"strings"
)

// Interface for sorting the record list by function name.
type recordByName []*clangdoc.RecordInfo

func (f recordByName) Len() int {
	return len(f)
}
func (f recordByName) Swap(i, j int) {
	f[i], f[j] = f[j], f[i]
}
func (f recordByName) Less(i, j int) bool {
	return f[i].Name < f[j].Name
}

// See seenFuncs variable in WriteClass reference for how it works. This function will avoid adding
// duplicates and will update the map as it adds functions.
func writeBaseClassFunctions(index *Index, r *clangdoc.RecordInfo, headingLevel int, seenFuncs map[string]bool, f io.Writer) {
	// Split out the public member functions.
	var funcs []*clangdoc.FunctionInfo
	for _, fn := range r.ChildFunctions {
		if fn.IsPublic() && !r.IsConstructor(fn) && !r.IsDestructor(fn) {
			key := fn.IdentityKey()
			if !seenFuncs[key] {
				seenFuncs[key] = true
				funcs = append(funcs, fn)
			}
		}
	}
	if len(funcs) == 0 {
		return
	}
	sort.Sort(functionByName(funcs))

	// Re-lookup this record by USR because the name contains the full context on the real
	// record but not the Base one.
	//
	// This can be null if we're not documenting the base class.
	fullRecord := index.Records[r.USR]

	url := recordLink(index, r)
	if len(url) == 0 {
		// When there's no full record, we need to use the Path+Name as the name (there's
		// no full Namespace record) and there is no documentation link.
		fmt.Fprintf(f, "%s Inherited from %s\n\n",
			headingMarkerAtLevel(headingLevel),
			clangdoc.PathNameToFullyQualified(r.Path, r.Name))
	} else {
		fmt.Fprintf(f, "%s Inherited from [%s](%s)\n\n",
			headingMarkerAtLevel(headingLevel), recordFullName(fullRecord), url)
	}

	if true {
		// Write out the functions as declarations.
		writePreHeader(f)
		for _, fn := range funcs {
			writeFunctionDeclaration(fn, "", true, memberFunctionLink(index, r, fn), f)
		}
		writePreFooter(f)
	} else {
		// Write out the functions (as links when possible).
		for _, fn := range funcs {
			url := memberFunctionLink(index, r, fn)
			if len(url) == 0 {
				fmt.Fprintf(f, "  - %s\n\n", fn.Name)
			} else {
				fmt.Fprintf(f, "  - [%s](%s)\n\n", fn.Name, url)
			}
		}
	}
}

func writeRecordReference(settings WriteSettings, index *Index, r *clangdoc.RecordInfo, f io.Writer) {
	fullName := recordFullName(r)
	// Devsite uses {:#htmlId} to give the title a custom ID.
	fmt.Fprintf(f, "## %s %s {:#%s}\n\n", r.TagType, fullName, recordHtmlId(index, r))

	// This prefix is used for function names. Don't include the full scope (like namespaces)
	// because this will be printed as a declaration where namespaces are not used. This will
	// look weird. There should be enough context with just the class name for it to make sense.
	namePrefix := r.Name + "::"

	fmt.Fprintf(f, "[Declaration source code](%s)\n\n", settings.locationSourceLink(r.DefLocation))

	// Split out the member functions.
	var funcs []*clangdoc.FunctionInfo
	var ctors []*clangdoc.FunctionInfo
	//var dtor clangdoc.FunctionInfo
	for _, fn := range r.ChildFunctions {
		if !fn.IsPublic() {
			continue
		} else if r.IsConstructor(fn) {
			ctors = append(ctors, fn)
		} else if r.IsDestructor(fn) {
			//dtor = fn;
		} else {
			funcs = append(funcs, fn)
		}
	}
	sort.Sort(functionByName(funcs))

	// Collect the public records to output.
	var dataMembers []clangdoc.MemberTypeInfo
	for _, m := range r.Members {
		if m.IsPublic() {
			dataMembers = append(dataMembers, m)
		}
	}

	writeRecordDeclarationBlock(index, r, dataMembers, f)
	writeComment(r.Description, markdownHeading2, f)

	// Data member documentation first.
	for _, d := range dataMembers {
		// Only add separate documentation when there is some. Unlike functions, the
		// class/struct code declaration includes all data members and our code often
		// doesn't have documentation for obvious ones.
		if len(d.Description) > 0 {
			// TODO needs anchor ref.
			fmt.Fprintf(f, "### %s\n\n", d.Name)
			writeComment(d.Description, markdownHeading3, f)
		}
	}

	if len(ctors) > 0 {
		fmt.Fprintf(f, "### Constructor {:#%s}\n\n", memberFunctionHtmlId(r, ctors[0]))
		// TODO merge adjacent constructors with no blanks or comments between then.
		for _, fn := range ctors {
			writeFunctionBody(fn, namePrefix, false, f)
			fmt.Fprintf(f, "\n")
		}
	}

	// TODO output child records with docstrings and enums.
	// Data member docstrings are not currently emitted by clang-doc.

	// Tracks whether we have seen a given function so we don't duplicate it. Functions can
	// appear multiple times, one for each base class they appear in and once for the class'
	// implementation. If an implemented function declaration in base class' base class, it will
	// appear three times, once for each base and once for the implementation.
	//
	// Here, we only want to show it the first time. This is indexed by each function's
	// IdentityKey().
	seenFuncs := make(map[string]bool)

	// This needs to be done in reverse order because the comments are normally associated with the
	// lowest-level base class that declares the function.
	for i := len(r.Bases) - 1; i >= 0; i-- {
		writeBaseClassFunctions(index, r.Bases[i], 3, seenFuncs, f)
	}

	// Member functions.
	for _, fn := range funcs {
		if !seenFuncs[fn.IdentityKey()] {
			// Don't fully qualify the name since including namespaces looks too noisy.
			// Just include the class name for clarity.
			fmt.Fprintf(f, "### %s::%s%s {:#%s}\n\n", r.Name, fn.Name, functionEllipsesParens(fn), memberFunctionHtmlId(r, fn))
			writeFunctionBody(fn, namePrefix, true, f)
			fmt.Fprintf(f, "\n")
		}
	}
}

func recordFullName(r *clangdoc.RecordInfo) string {
	result := ""

	// The order is in reverse of C++.
	for i := len(r.Namespace) - 1; i >= 0; i-- {
		result += r.Namespace[i].Name + "::"
	}
	return result + r.Name
}

// Returns the empty string if the record does not have emitted documentation.
func recordHtmlId(index *Index, r *clangdoc.RecordInfo) string {
	// Use the fully-qualified type name as the ID (may need disambiguating in the future).
	fullRecord := index.Records[r.USR]
	if fullRecord == nil {
		return ""
	}
	return recordFullName(fullRecord)
}

// Returns the empty string if the record does not have emitted documentation.
func recordLink(index *Index, r *clangdoc.RecordInfo) string {
	fullRecord := index.Records[r.USR]
	if fullRecord == nil {
		return ""
	}
	return HeaderReferenceFile(fullRecord.DefLocation.Filename) + "#" + recordHtmlId(index, r)
}

// Use for standalone functions. For member functions use memberFunctionHtmlId()
func memberFunctionHtmlId(r *clangdoc.RecordInfo, f *clangdoc.FunctionInfo) string {
	// Need to fully qualify the function with the class for uniqueness even if the title
	// doesn't have this information.
	return recordFullName(r) + "::" + f.Name
}

// Returns the empty string if the record isn't documented.
func memberFunctionLink(index *Index, r *clangdoc.RecordInfo, f *clangdoc.FunctionInfo) string {
	fullRecord := index.Records[r.USR]
	if fullRecord == nil {
		return ""
	}
	return HeaderReferenceFile(fullRecord.DefLocation.Filename) + "#" + memberFunctionHtmlId(fullRecord, f)
}

func recordKind(r *clangdoc.RecordInfo) string {
	// The TagType matches the C++ construct except for capitalization ("Class", "Struct", ...).
	if len(r.TagType) == 0 {
		// TODO clang-doc doesn't output a tag type for some structs. For example,
		// in fdio: "typedef vnattr { ... } vnattr_t;" gets no TagType.
		return "ClangDocTodoMissingTagType"
	}
	return strings.ToLower(r.TagType)
}

func writeRecordDeclarationBlock(index *Index, r *clangdoc.RecordInfo, data []clangdoc.MemberTypeInfo, f io.Writer) {
	// TODO omit this whole function if there are no namespaces, base classes, or members
	// ("class Foo {...};" is pointless by itself).
	writePreHeader(f)

	// This includes any parent struct/class name but not namespaces.
	var namePrefix string

	// Namespaces. The order is in reverse of C++ (most specific first).
	var nsBegin, nsEnd string
	for i := len(r.Namespace) - 1; i >= 0; i-- {
		ns := r.Namespace[i]
		if ns.Type == "Namespace" {
			if ns.Name != "GlobalNamespace" {
				// TODO clang-doc seems to output "GlobalNamespace" namespace
				// qualifications for structs in the global namespace. I think this
				// is incorrect and these should just be omitted.
				nsBegin += fmt.Sprintf("<span class=\"kwd\">namespace</span> %s {\n", ns.Name)
				nsEnd = fmt.Sprintf("}  <span class=\"com\">// namespace %s</span>\n", ns.Name) + nsEnd
			}
		} else {
			namePrefix += ns.Name + "::"
		}
	}
	if len(nsBegin) > 0 {
		fmt.Fprintf(f, "%s\n", nsBegin)
	}

	fmt.Fprintf(f, "<span class=\"kwd\">%s</span> <span class=\"typ\">%s%s</span>", recordKind(r), namePrefix, r.Name)

	// Base and virtual base class records. The direct base classes are in Parents and
	// VirtualParents but this does not have the access record on it (public/private/protected).
	// The Bases list is in the correct order so use that as the primary key, and look up those
	// in [Virtual]Parents to see if they're direct parents.
	parents := make(map[string]bool) // Set of USR ids of direct parents.
	for _, p := range r.Parents {
		parents[p.USR] = true
	}
	for _, p := range r.VirtualParents {
		parents[p.USR] = true
	}

	emittedBase := false
	for _, b := range r.Bases {
		if !parents[b.USR] {
			continue // Not a direct parent.
		}

		if !emittedBase {
			fmt.Fprintf(f, " :")
			emittedBase = true
		} else {
			fmt.Fprintf(f, ",")
		}

		if len(b.Access) > 0 {
			fmt.Fprintf(f, " <span class=\"kwd\">%s</span>", strings.ToLower(b.Access))
		}
		if b.IsVirtual {
			fmt.Fprintf(f, " <span class=\"kwd\">virtual</span>")
		}

		baseType := clangdoc.Type{Name: b.Name, Path: b.Path}
		baseName, _ := getEscapedTypeName(baseType)

		url := recordLink(index, b)
		if len(url) == 0 {
			fmt.Fprintf(f, " <span class=\"typ\">%s</span>", baseName)
		} else {
			fmt.Fprintf(f, " <span class=\"typ\"><a href=\"%s\">%s</a></span>", url, baseName)
		}
	}

	fmt.Fprintf(f, " {")
	if len(data) == 0 {
		// No members, just output "Foo { ... }" since the functions will be documented
		// later and we don't want to imply the class is empty.
		fmt.Fprintf(f, " <span class=\"com\">...</span> ")
	} else {
		// Output data members.
		fmt.Fprintf(f, "\n")
		for _, d := range data {
			tn, _ := getEscapedTypeName(d.Type)
			fmt.Fprintf(f, "    <span class=\"typ\">%s</span> %s;\n", tn, d.Name)
		}
	}

	fmt.Fprintf(f, "};\n")

	if len(nsEnd) > 0 {
		fmt.Fprintf(f, "\n")
		fmt.Fprintf(f, nsEnd)
	}

	writePreFooter(f)
}
