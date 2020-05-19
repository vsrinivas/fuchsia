// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package link

import (
	"strings"
)

// TODO(fxbug.dev/52260) replace this with a better alternative.
type multiErr struct {
	errs []error
}

func (m multiErr) Error() string {
	if len(m.errs) == 0 {
		panic("Should never instantiate multiErr with zero errors")
	}
	if len(m.errs) == 1 {
		return m.errs[0].Error()
	}

	var b strings.Builder
	for i, e := range m.errs {
		if i != 0 {
			b.WriteString("; ")
		}
		b.WriteString(e.Error())
	}
	return b.String()
}

func CombineErrors(errs ...error) error {
	use := make([]error, 0, len(errs))
	for _, err := range errs {
		if err != nil {
			use = append(use, err)
		}
	}
	if len(use) == 0 {
		return nil
	}
	return multiErr{errs: use}
}

func AppendError(s, e error) error {
	if e == nil {
		return s
	}
	if m, ok := s.(multiErr); ok {
		return multiErr{errs: append(m.errs, e)}
	}
	if s == nil {
		return multiErr{errs: []error{e}}
	}
	return multiErr{errs: []error{s, e}}
}
