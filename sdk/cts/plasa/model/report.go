// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

package model

import (
	"encoding/json"
	"fmt"
	"io"
	"regexp"
	"sort"
	"strings"
)

// ReportKind is the type of a platform surface element.
type ReportKind string

const (
	ReportKindFunction     ReportKind = "function"
	ReportKindMethod       ReportKind = "method"
	ReportKindEnum         ReportKind = "enum"
	ReportKindEnumMember   ReportKind = "enum/member"
	ReportKindRecord       ReportKind = "record"
	ReportKindRecordMember ReportKind = "record/member"
)

var reportKindStringToEnum = map[string]ReportKind{
	"function":      ReportKindFunction,
	"method":        ReportKindMethod,
	"enum":          ReportKindEnum,
	"enum/member":   ReportKindEnumMember,
	"record":        ReportKindRecord,
	"record/member": ReportKindRecordMember,
}

var _ json.Marshaler = (*ReportKind)(nil)

func (r ReportKind) MarshalJSON() ([]byte, error) {
	return []byte(fmt.Sprintf("%q", r)), nil
}

var _ json.Unmarshaler = (*ReportKind)(nil)

func (r *ReportKind) UnmarshalJSON(value []byte) error {
	var s string
	if err := json.Unmarshal(value, &s); err != nil {
		return fmt.Errorf("while recovering %v into string: %w", string(value), err)
	}
	var (
		e  ReportKind
		ok bool
	)
	if e, ok = reportKindStringToEnum[s]; !ok {
		return fmt.Errorf("could not parse ReportKind from %v", s)
	}
	*r = e
	return nil
}

// ReportAccess is the type of access of a platform surface element.
type ReportAccess string

const (
	ReportAccessPublic    ReportAccess = "public"
	ReportAccessProtected ReportAccess = "protected"
	ReportAccessPrivate   ReportAccess = "private"
)

var reportAccessStringToEnum = map[string]ReportAccess{
	"":          ReportAccessPublic,
	"public":    ReportAccessPublic,
	"protected": ReportAccessProtected,
	"private":   ReportAccessPrivate,
}

var accessToReportAccess = map[Access]ReportAccess{
	AccessPublic:    ReportAccessPublic,
	AccessPrivate:   ReportAccessPrivate,
	AccessProtected: ReportAccessProtected,
}

var _ json.Marshaler = (*ReportAccess)(nil)

func (r ReportAccess) MarshalJSON() ([]byte, error) {
	return []byte(fmt.Sprintf("%q", r)), nil
}

var _ json.Unmarshaler = (*ReportAccess)(nil)

func (r *ReportAccess) UnmarshalJSON(value []byte) error {
	var s string
	if err := json.Unmarshal(value, &s); err != nil {
		return fmt.Errorf("while recovering %v into string: %w", string(value), err)
	}
	var (
		e  ReportAccess
		ok bool
	)
	if e, ok = reportAccessStringToEnum[s]; !ok {
		return fmt.Errorf("could not parse ReportAccess from %v", s)
	}
	*r = e
	return nil
}

// ReportItem is the format of a single report item. Expect this struct will
// grow over time to include more extracted information.
type ReportItem struct {
	Name string `json:"name"`
	// Filename is the file name in which the report item can be found.
	Filename string `json:"file,omitempty"`
	// Line is the line number (1-based) in Filename where the report item is defined.
	LineNumber int `json:"line,omitempty"`
	// Kind is the type of the item.  It is required.
	Kind ReportKind `json:"kind"`
	// Type is the declaration type of an item, if there is one.
	Type string `json:"type,omitempty"`
	// ReturnType is a return type of a function or method, if there is one.
	ReturnType string `json:"return_type,omitempty"`
	// Params is the type declaration of all parameters, if there are some.
	Params string `json:"params,omitempty"`
	// Access lets us know if the item is publicly accessible.  If omitted,
	// the default is public.
	Access ReportAccess `json:"access,omitempty"`
}

// Report is the format of the output report for the clang doc filter.
type Report struct {
	Items []ReportItem `json:"items"`

	// fileregex contains the file regexes to include when adding file names to report, if set.
	// If unset, it has no effect.
	fileregex []regexp.Regexp

	// symregex contains the file regexes to include when adding symbol names to report, if set.
	// If unset, it has no effect.
	symregex []regexp.Regexp
}

// SetFileRegexes sets the regular expressions used to match the filename of
// the symbols.  Only symbols that match at least one regexp will be included,
// if the regexp is specified.  Otherwise, everything matches.
func (r *Report) SetFileRegexes(regexes []string) error {
	return addRegexes(&r.fileregex, regexes)
}

func (r *Report) SetSymRegexes(regexes []string) error {
	return addRegexes(&r.symregex, regexes)
}

// addRegexes compiles and adds regexes to a list.
func addRegexes(r *[]regexp.Regexp, regexes []string) error {
	for _, rx := range regexes {
		rc, err := regexp.Compile(rx)
		if err != nil {
			return fmt.Errorf("could not parse regexp: %+v: %w", r, err)
		}
		*r = append(*r, *rc)
	}
	return nil
}

func (r *Report) matchFilename(fn string) bool {
	return matchToAnyRegex(fn, r.fileregex)
}

func (r *Report) matchSymbol(sym string) bool {
	return matchToAnyRegex(sym, r.symregex)
}

func matchToAnyRegex(sym string, regexes []regexp.Regexp) bool {
	if len(regexes) == 0 {
		// If we defined no regexes, match everything.
		return true
	}
	for _, rx := range regexes {
		if rx.Match([]byte(sym)) {
			return true
		}
	}
	return false
}

// AddEnumMember adds an enum member to the plasa report.
func (r *Report) AddEnumMember(enumName, fn, member string) error {
	fullName := fullNameMulticomponent([]ID{}, enumName, member)
	if r.matchFilename(fn) && r.matchSymbol(fullName) {
		i := ReportItem{
			Name: fullName,
			Kind: ReportKindEnumMember,
		}
		r.Items = append(r.Items, i)
	}
	return nil
}

// AddChildEnum adds the child enum and its members to the plasa report.
func (r *Report) AddChildEnum(c ChildEnum) error {
	// Push this into ChildEnum.
	fullName := fullName(c.Name, c.Namespace)
	fn := c.DefLocation.Filename
	if r.matchFilename(fn) && r.matchSymbol(fullName) {
		i := ReportItem{
			Name: fullName,
			Kind: ReportKindEnum,
		}
		r.Items = append(r.Items, i)
	}

	// Add each enum member too.
	for _, m := range c.Members {
		if err := r.AddEnumMember(fullName, fn, m); err != nil {
			return fmt.Errorf("while adding enum member: %v::%v: %w", fullName, m, err)
		}
	}
	return nil
}

func (r *Report) AddChildFunction(c ChildFunction) error {
	f := c.fullName()
	n := c.DefLocation.Filename
	if r.matchFilename(n) && r.matchSymbol(f) {
		i := ReportItem{
			Name:       f,
			Filename:   n,
			LineNumber: c.DefLocation.LineNumber,
			ReturnType: c.ReturnType.TypeName(),
		}
		if c.IsMethod {
			i.Kind = ReportKindMethod
		} else {
			i.Kind = ReportKindFunction
		}
		var params []string
		for _, p := range c.Params {
			params = append(params, p.TypeName())
		}
		i.Params = fmt.Sprintf("(%v)", strings.Join(params, ","))

		// Needs return type and params.
		r.Items = append(r.Items, i)
	}
	return nil
}

func (r *Report) AddMember(parentName string, c Member) error {
	fullName := fullNameMulticomponent([]ID{}, parentName, string(c.Name))
	i := ReportItem{
		Name:   fullName,
		Kind:   ReportKindRecordMember,
		Type:   c.Type.GetTypeString(),
		Access: accessToReportAccess[c.Access],
	}
	r.Items = append(r.Items, i)
	return nil
}

// Add inserts an Aggregate into the report.
func (r *Report) AddAggregate(a Aggregate) error {
	f := fullName(a.Name, a.Namespace)
	n := a.DefLocation.Filename
	if r.matchFilename(n) && r.matchSymbol(f) {
		i := ReportItem{
			Name:       f,
			Kind:       ReportKindRecord,
			LineNumber: a.DefLocation.LineNumber,
			Filename:   n,
			Access:     accessToReportAccess[a.Access],
		}
		r.Items = append(r.Items, i)
	}

	// Recurse into components and ship them out too.
	for _, c := range a.ChildEnums {
		if err := r.AddChildEnum(c); err != nil {
			return fmt.Errorf("while adding child enum: %v: %w", c, err)
		}
	}
	for _, c := range a.ChildFunctions {
		if err := r.AddChildFunction(c); err != nil {
			return fmt.Errorf("while adding child function: %v: %w", c, err)
		}
	}
	for _, c := range a.Members {
		if err := r.AddMember(f, c); err != nil {
			return fmt.Errorf("while adding a member: %v::%v: %w", f, c, err)
		}
	}
	return nil
}

// WriteJSON writes the contents of the report in JSON format to the supplied
// writer.
func (r Report) WriteJSON(w io.Writer) error {
	// Ensure the output is stable.
	if r.Items != nil {
		sort.SliceStable(r.Items, func(i, j int) bool {
			return strings.ToLower(r.Items[i].Name) < strings.ToLower(r.Items[j].Name)
		})
	}
	e := json.NewEncoder(w)
	// We do not expect to need HTML escaping.
	e.SetEscapeHTML(false)
	// This indentation format is compatible with `fx format-code`.
	e.SetIndent("", "    ")
	if err := e.Encode(r); err != nil {
		return fmt.Errorf("while encoding JSON output: %w", err)
	}
	return nil
}

// ReadReportJSON reads the contents of the report in JSON format from the
// supplied reader.
func ReadReportJSON(r io.Reader) (Report, error) {
	d := json.NewDecoder(r)
	// Verifying the parsing gets confusing if we're lenient about unknown
	// fields.
	d.DisallowUnknownFields()
	var ret Report
	if err := d.Decode(&ret); err != nil {
		return ret, fmt.Errorf("while reading Report as JSON: %w:", err)
	}
	return ret, nil
}
