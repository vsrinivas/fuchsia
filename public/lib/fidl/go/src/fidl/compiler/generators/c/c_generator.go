// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"os"
	"text/template"

	"mojom/generators/c/cgen"
	"mojom/generators/c/templates"
	"mojom/generators/common"
)

func main() {
	tmpls := template.New("CTemplates")
	template.Must(tmpls.Parse(templates.GenerateHeaderFile))
	template.Must(tmpls.Parse(templates.GenerateSourceFile))
	template.Must(tmpls.Parse(templates.GenerateEnum))
	template.Must(tmpls.Parse(templates.GenerateStructDeclarations))
	template.Must(tmpls.Parse(templates.GenerateStructDefinitions))
	template.Must(tmpls.Parse(templates.GenerateUnion))
	template.Must(tmpls.Parse(templates.GenerateInterfaceDeclarations))
	template.Must(tmpls.Parse(templates.GenerateInterfaceDefinitions))
	template.Must(tmpls.Parse(templates.GenerateTypeTableDeclarations))
	template.Must(tmpls.Parse(templates.GenerateTypeTableDefinitions))

	config := common.GetCliConfig(os.Args)
	common.GenerateOutput(func(fileName string, config common.GeneratorConfig) {
		headerWriter := common.OutputWriterByFilePath(fileName, config, ".mojom-c.h")
		sourceWriter := common.OutputWriterByFilePath(fileName, config, ".mojom-c.c")
		mojomFile := config.FileGraph().Files[fileName]
		headerInfo := cgen.NewHeaderTemplate(config.FileGraph(), &mojomFile, config.SrcRootPath())
		sourceInfo := cgen.NewSourceTemplate(config.FileGraph(), &mojomFile, config.SrcRootPath(), &headerInfo)

		if err := tmpls.ExecuteTemplate(headerWriter, "GenerateHeaderFile", headerInfo); err != nil {
			log.Fatal(err)
		}
		if err := tmpls.ExecuteTemplate(sourceWriter, "GenerateSourceFile", sourceInfo); err != nil {
			log.Fatal(err)
		}

		log.Printf("Processed %s", fileName)
	}, config)
}
