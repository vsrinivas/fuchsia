// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"strings"
)

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
