// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"fmt"
	"path/filepath"
	"strings"
	"unicode"
	"unicode/utf8"

	"fidl/compiler/generated/fidl_types"
)

func (t *translator) goTypeName(typeKey string) string {
	if cachedType, ok := t.goTypeCache[typeKey]; ok {
		return cachedType
	}

	userDefinedType := t.fileGraph.ResolvedTypes[typeKey]
	shortName := userDefinedTypeShortName(userDefinedType)

	// TODO(azani): Resolve conflicts somehow.
	goType := formatName(shortName)

	if e, ok := userDefinedType.(*fidl_types.UserDefinedTypeEnumType); ok {
		if e.Value.DeclData.ContainerTypeKey != nil {
			containerName := t.goTypeName(*e.Value.DeclData.ContainerTypeKey)
			goType = fmt.Sprintf("%s_%s", containerName, goType)
		}
	}

	t.goTypeCache[typeKey] = goType
	return goType
}

func (t *translator) goConstName(constKey string) (name string) {
	if cachedName, ok := t.goConstNameCache[constKey]; ok {
		return cachedName
	}

	declaredConstant := t.fileGraph.ResolvedConstants[constKey]

	name = formatName(*declaredConstant.DeclData.ShortName)
	if declaredConstant.DeclData.ContainerTypeKey != nil {
		containerName := t.goTypeName(*declaredConstant.DeclData.ContainerTypeKey)
		name = fmt.Sprintf("%s_%s", containerName, name)
	}

	return
}

func fileNameToPackageName(fileName string) string {
	base := filepath.Base(fileName)
	ext := filepath.Ext(base)
	// It's technically bad style if the package of a file is not the
	// same as the directory it's in, but it just works anyway, while
	// a package name that is not an identifier is a syntax error.
	return strings.Replace(strings.Replace(base[:len(base)-len(ext)], "-", "_", -1), ".", "_", -1)
}

func (t *translator) importFidlFile(fileName string) {
	pkgName := fileNameToPackageName(fileName)
	pkgPath, err := filepath.Rel(t.Config.SrcRootPath(), fileName)
	if err != nil {
		panic(err.Error())
	}
	pkgPath = pkgPath[:len(pkgPath)-len(filepath.Ext(pkgPath))]
	t.imports[pkgName] = pkgPath
}

func userDefinedTypeDeclData(userDefinedType fidl_types.UserDefinedType) *fidl_types.DeclarationData {
	switch u := userDefinedType.(type) {
	case *fidl_types.UserDefinedTypeEnumType:
		return u.Value.DeclData
	case *fidl_types.UserDefinedTypeStructType:
		return u.Value.DeclData
	case *fidl_types.UserDefinedTypeUnionType:
		return u.Value.DeclData
	case *fidl_types.UserDefinedTypeInterfaceType:
		return u.Value.DeclData
	}
	panic("Non-handled mojom UserDefinedType. This should never happen.")
}

// userDefinedTypeShortName extracts the ShortName from a user-defined type.
func userDefinedTypeShortName(userDefinedType fidl_types.UserDefinedType) string {
	return *userDefinedTypeDeclData(userDefinedType).ShortName
}

// privateName accepts a string and returns that same string with the first rune
// set to lowercase.
func privateName(name string) string {
	_, size := utf8.DecodeRuneInString(name)
	return strings.ToLower(name[:size]) + name[size:]
}

// formatName formats a name to match go style.
func formatName(name string) string {
	var parts []string
	for _, namePart := range strings.Split(name, "_") {
		partStart := 0
		lastRune, _ := utf8.DecodeRuneInString(name)
		lastRuneStart := 0
		for i, curRune := range namePart {
			if i == 0 {
				continue
			}

			if unicode.IsUpper(curRune) && !unicode.IsUpper(lastRune) {
				parts = append(parts, namePart[partStart:i])
				partStart = i
			}

			if !(unicode.IsUpper(curRune) || unicode.IsDigit(curRune)) && unicode.IsUpper(lastRune) && partStart != lastRuneStart {
				parts = append(parts, namePart[partStart:lastRuneStart])
				partStart = lastRuneStart
			}

			lastRuneStart = i
			lastRune = curRune
		}
		parts = append(parts, namePart[partStart:])
	}

	for i := range parts {
		part := strings.Title(strings.ToLower(parts[i]))
		_, size := utf8.DecodeRuneInString(part)
		parts[i] = strings.ToUpper(part[:size]) + part[size:]
	}

	return strings.Join(parts, "")
}
