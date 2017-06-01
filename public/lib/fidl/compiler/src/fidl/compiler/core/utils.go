// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package core

import (
	"fidl/compiler/lexer"
	"fmt"
	"os"
	"path/filepath"
)

// RelPathIfShorter() returns a file path equivalent to |filePath|, relative
// to the current working directory. It attempts to express |filePath| as a
// path relative to the current working diretory and will return that relative
// path if it is shorter than |filePath|. If it is unable to construct such a
// relative file path or if the resulting relative file path is longer, then the
// original |filePath| is returned.
func RelPathIfShorter(filePath string) string {
	if cwd, err := os.Getwd(); err == nil {
		if relPath, err := filepath.Rel(cwd, filePath); err == nil {
			if len(relPath) < len(filePath) {
				filePath = relPath
			}
		}
	}
	return filePath
}

// UserErrorMessage is responsible for formatting user-facing error messages
// in a uniform way.
// file: The MojomFile in which the error occurs
// token: The token most closely associated with the problem.
// message: The base error text. It should not include any location information
// as that will be added by this function. It should also not include a prefix
// such as "Error:" because that will be added by this function.
func UserErrorMessage(file *MojomFile, token lexer.Token, message string) string {
	return UserErrorMessageP(file, token, "Error:", message)
}

// UserErrorMessageP is responsible for formatting user-facing error messages
// This function is similar to UserErrorMessage except that it also allows the
// caller to specify the message prefix.
// prefix: An error message prefix such as "Error:". It should not end with a space.
func UserErrorMessageP(file *MojomFile, token lexer.Token, prefix, message string) string {
	// TODO(rudominer) Allow users to disable the use of color in snippets.
	useColor := true

	// Optionally color the prefix red.
	if useColor && len(prefix) > 0 {
		prefix = fmt.Sprintf("\x1b[31;1m%s\x1b[0m", prefix)
	}
	if len(prefix) > 0 {
		prefix = fmt.Sprintf("%s ", prefix)
	}
	message = fmt.Sprintf("%s%s", prefix, message)

	filePath := "Unknown file"
	importedFromMessage := ""
	snippet := ""
	if file != nil {
		if len(file.fileContents) > 0 {
			snippet = fmt.Sprintf("\n%s", token.Snippet(file.fileContents, useColor))
		}
		filePath = RelPathIfShorter(file.CanonicalFileName)
		importedFromMessage = file.ImportedFromMessage()
	}
	return fmt.Sprintf("\n%s:%s: %s%s\n%s", filePath, token.ShortLocationString(),
		message, snippet, importedFromMessage)
}
