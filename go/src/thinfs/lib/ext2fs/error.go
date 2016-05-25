// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package ext2fs

// #include <com_err.h>
import "C"

import (
	"fmt"
	"os"

	"github.com/pkg/errors"
)

var (
	// ErrAlreadyExists indicates that requested resource already exists.
	ErrAlreadyExists = os.ErrExist

	// ErrNotFound indicates that the requested resource was not found.
	ErrNotFound = os.ErrNotExist

	// ErrNotEmpty indicates that the caller attempted to remove a non-empty directory.
	ErrNotEmpty = errors.New("directory is not empty")

	// ErrNotOpen indicates that the caller is attempting to close a file or directory
	// that is not currently open.
	ErrNotOpen = errors.New("file or directory not open")

	// ErrNotAFile indicatess that the caller attempted to open a file using a path
	// that did not point to a file.
	ErrNotAFile = errors.New("not a file")

	// ErrNotADir indicates that the caller attempted to open a directory using a path
	// that did not point to a directory.
	ErrNotADir = errors.New("not a directory")

	// ErrIsActive indicates that the caller attempted to unlink a directory with active
	// references.
	ErrIsActive = errors.New("directory has active references")
)

// check is a catch-all function for converting an errcode_t reported by libext2fs into
// an error that Go can understand.
func check(err C.errcode_t) error {
	if err != C.errcode_t(0) {
		return fmt.Errorf("%s", C.GoString(C.error_message(C.long(err))))
	}

	return nil
}
