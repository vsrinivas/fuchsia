// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

// deps_generator.go implements the main function of the deps generator.
// The deps generator generates .d files. The syntax is that which is used to
// specify dependencies in makefiles.

import (
	"log"
	"os"
	"path"
	"path/filepath"

	"fidl/compiler/generators/common"
)

func main() {
	log.SetFlags(0)
	config := common.GetCliConfig(os.Args)
	common.GenerateOutput(WriteDepsFile, config)
}

// WriteDepsFile writes a .d file for the specified file.
func WriteDepsFile(fileName string, config common.GeneratorConfig) {
	writer := common.OutputWriterByFilePath(fileName, config, ".d")
	dFileName := fileName[:len(fileName)-len(filepath.Ext(fileName))] + ".d"
	writer.WriteString(path.Base(dFileName))
	writer.WriteString(" : ")

	imports := GetTransitiveClosure(fileName, config)
	for _, imported := range imports {
		writer.WriteString(imported)
		writer.WriteString(" ")
	}
	writer.WriteString("\n")
}

// GetTransitiveClosure gets the list of transitive imports starting with
// rootFile. rootFile itself is not included.
// The imports are specified as paths relative to the directory in which
// rootFile is found.
func GetTransitiveClosure(rootFile string, config common.GeneratorConfig) (result []string) {
	fileGraph := config.FileGraph()
	toVisit := []string{rootFile}
	rootFileDir := path.Dir(rootFile)

	for len(toVisit) > 0 {
		curFileName := toVisit[len(toVisit)-1]
		toVisit = toVisit[1:len(toVisit)]

		curFile := fileGraph.Files[curFileName]
		rel, err := filepath.Rel(rootFileDir, curFileName)
		if err != nil {
			log.Fatalln(err.Error())
		}
		result = append(result, rel)

		if curFile.Imports != nil {
			for _, importFileName := range *curFile.Imports {
				toVisit = append(toVisit, importFileName)
			}
		}
	}
	return
}
