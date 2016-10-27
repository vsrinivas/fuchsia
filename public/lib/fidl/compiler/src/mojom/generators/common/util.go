// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"log"
	"os"
	"path"
	"path/filepath"
)

// TODO(vardhan): Delete this function and move it into the C generator.
// OutputWriterByFilePath returns |Writer| that writes to a file whose relative
// path from config.OutputDir() is equivalent to the relative path from
// config.SrcRootPath() to fileName. Also, the extension of the file is changed
// from ".mojom" to |ext|.
//
// e.g. If
// ext = ".d"
// fileName = /alpha/beta/gamma/file.mojom
// SrcRootPath = /alpha/
// OutputDir = /some/output/path/
//
// The writer writes to /some/output/path/beta/gamma/file.d
func OutputWriterByFilePath(fileName string, config GeneratorConfig, ext string) Writer {
	fileName = changeExt(fileName, ext)
	outPath := outputFileByFilePath(fileName, config)

	return createAndOpen(outPath)
}

// outputFileByFilePath computes a file path such that the relative path
// from config.SrcRootPath() to the returned path is equivalent to the relative
// path from config.SrcRootPath() to fileName.
//
// e.g. If
// fileName = /alpha/beta/gamma/file.mojom
// SrcRootPath = /alpha/
// OutputDir = /some/output/path/
//
// The returned path is /some/output/path/beta/gamma/file.mojom
func outputFileByFilePath(fileName string, config GeneratorConfig) string {
	var err error
	relFileName, err := filepath.Rel(config.SrcRootPath(), fileName)
	if err != nil {
		log.Fatalln(err.Error())
	}
	return filepath.Join(config.OutputDir(), relFileName)
}

// createAndOpen opens for writing the specified file. If the file or any part
// of the directory structure in its path does not exist, they are created.
func createAndOpen(outPath string) (file Writer) {
	// Create the directory that will contain the output.
	outDir := path.Dir(outPath)
	if err := os.MkdirAll(outDir, os.ModeDir|0777); err != nil && !os.IsExist(err) {
		log.Fatalln(err.Error())
	}

	var err error
	file, err = os.OpenFile(outPath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0666)
	if err != nil {
		log.Fatalln(err.Error())
	}

	return
}

// changeExt changes the extension of a file to the specified extension.
func changeExt(fileName string, ext string) string {
	return fileName[:len(fileName)-len(filepath.Ext(fileName))] + ext
}
