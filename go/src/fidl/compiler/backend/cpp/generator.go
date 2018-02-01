// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	"os"
	"path/filepath"
	"text/template"
	"fidl/compiler/backend/types"
	"fidl/compiler/backend/cpp/templates"
)

type FidlGenerator struct{}

const ownerReadWriteNoExecute = 0644

const headerFileTemplate = `
{{- define "GenerateHeaderFile" -}}
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include "lib/fidl/cpp/bindings2/interface_ptr.h"
#include "lib/fidl/cpp/bindings2/internal/proxy_controller.h"
#include "lib/fidl/cpp/bindings2/internal/stub_controller.h"
#include "lib/fidl/cpp/bindings2/string.h"
    {{ range $interface := .InterfaceDeclarations }}
{{ template "InterfaceDeclaration" $interface }}
	{{- end }}
{{- end -}}
`

const implementationFileTemplate = `
{{- define "GenerateImplementationFile" -}}
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <zircon/assert.h>

#include <memory>

#include "lib/fidl/cpp/bindings2/test/fidl_types.h"

{{ range $interface := .InterfaceDeclarations }}
{{ template "InterfaceDefinition" $interface }}
	{{ end }}
{{- end -}}
`

func writeFile(outputFilename string,
			   templateName string,
			   tmpls *template.Template,
			   fidlData types.Root) error {
	f, err := os.Create(outputFilename)
	if err != nil {
		return err
	}
	defer f.Close()
	return tmpls.ExecuteTemplate(f, templateName, fidlData)
}

func (_ FidlGenerator) GenerateFidl(
	fidlData types.Root, _args []string,
	outputDir string, srcRootPath string) error {
	parentDir := filepath.Join(outputDir, srcRootPath)
	err := os.MkdirAll(parentDir, ownerReadWriteNoExecute)
	if err != nil {
		return err
	}

	tmpls := template.New("CPPTemplates")
	template.Must(tmpls.Parse(headerFileTemplate))
	template.Must(tmpls.Parse(implementationFileTemplate))
	template.Must(tmpls.Parse(templates.Interface))

	outputFilename := filepath.Join(parentDir, "generated.h")
	err = writeFile(outputFilename, "GenerateHeaderFile", tmpls, fidlData)
	if err != nil {
		return err
	}

	outputFilename = filepath.Join(parentDir, "generated.cc")
	return writeFile(outputFilename, "GenerateImplementationFile", tmpls, fidlData)
}
