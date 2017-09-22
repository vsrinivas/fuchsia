// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"strings"
)

// main_utils contains functions and type definitions that are used by the mojom
// tool commands and implement logic that is not specific to any one command.

// CommaSeparatedList holds the result of parsing a command-line flag that
// accepts a comma-separated list of strings. This type satisfies the flag.Value
// interface.
type CommaSeparatedList []string

func (l *CommaSeparatedList) String() string {
	return fmt.Sprintf("%v", *l)
}

func (l *CommaSeparatedList) Set(args string) error {
	for _, el := range strings.Split(args, ",") {
		*l = append(*l, el)
	}
	return nil
}

// RepeatedStringArg holds the result of parsing a string command-line argument
// that can be repeated. This type satisfies the flag.Value interface.
type RepeatedStringArg []string

func (r *RepeatedStringArg) String() string {
	return fmt.Sprintf("%v", *r)
}

func (r *RepeatedStringArg) Set(arg string) error {
	*r = append(*r, arg)
	return nil
}

// DirectoryList holds the result of parsing a command-line flag
// that accepts a comma-separated list of directory paths. This
// type satisfies the flag.Value interface.
type DirectoryList []string

func (dl *DirectoryList) String() string {
	return fmt.Sprintf("%v", *dl)
}

func (dl *DirectoryList) Set(args string) error {
	for _, name := range strings.Split(args, ",") {
		*dl = append(*dl, name)
	}
	return nil
}
