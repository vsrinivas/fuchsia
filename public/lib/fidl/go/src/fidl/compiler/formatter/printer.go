// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package formatter

import (
	"bytes"
	"fmt"
	"fidl/compiler/lexer"
	"fidl/compiler/core"
	"sort"
	"strings"
)

// printer is a single-use struct that keeps track of the state necessary to
// pretty-print a mojom file.
// It is used by FormatMojom (see formatter.go)
//
// Internall, the entry point is writeMojomFile and the result is obtained by
// calling result.
//
// The pretty-printing of individual elements is implemented using the write.*
// methods. Those methods accept the element to be printed (or the element
// of which a portion is to be printed) and write the pretty-printed output
// to |buffer|.
type printer struct {
	// used indicates whether the printer has been used or not. If set to true,
	// writeMojomFile will panic.
	used bool

	// buffer is the buffer to which the write.* methods write the pretty-printed
	// mojom file element.
	buffer bytes.Buffer

	// indentSize is the current size of the indentation in number of space runes.
	indentSize int

	// eolComment is the comment to be printed at the end of the current line.
	eolComment *lexer.Token

	// linePos is the number of runes that have been written to the current line.
	linePos int

	// maxLineLength is the maximum number of runes that should be printed on a line.
	// A negative maxLineLength indicates no maximum line length should be enforced.
	// This is currently only used to decide when to break up a method on different lines.
	maxLineLength int

	// mojomFile is the file being printed.
	mojomFile *core.MojomFile

	// noFormat indicates we are in a block where formatting has been disabled.
	noFormat bool

	// token that begins a block where formatting has been disabled.
	noFormatStart lexer.Token

	// indentSize at the time noFormat is set.
	noFormatStartIndent int
}

// newPrinter is a constructor for printer.
func newPrinter() (p *printer) {
	p = new(printer)
	p.maxLineLength = 80
	return
}

// result returns the pretty-printed version of what was given to the printer.
// Usually, this is a pretty-printed MojomFile.
func (p *printer) result() string {
	return p.buffer.String()
}

// writeMojomFile is the entry point of the pretty printer.
// It takes a mojom file with attached comments and creates a pretty-printed
// representation.
func (p *printer) writeMojomFile(mojomFile *core.MojomFile) {
	if p.used {
		panic("A printer can only be used once!")
	}
	p.used = true
	p.mojomFile = mojomFile

	p.writeAttributes(mojomFile.Attributes)
	p.writeModuleNamespace(mojomFile.ModuleNamespace)

	if len(mojomFile.Imports) > 0 {
		p.writeImportedFiles(mojomFile.Imports)
	}

	for _, declaredObject := range mojomFile.DeclaredObjects {
		p.writeDeclaredObject(declaredObject)
		p.nl()
	}

	if mojomFile.FinalComments != nil {
		finalComments := trimEmptyLinesEnd(mojomFile.FinalComments)
		if len(finalComments) > 0 {
			p.nl()
			p.writeCommentBlocks(finalComments, true)
		}
	}

	p.eofNoFormat()
}

// writeModuleNamespace writes a mojom file's module statement and associated
// comments.
func (p *printer) writeModuleNamespace(module *core.ModuleNamespace) {
	if module == nil || module.Identifier == "" {
		return
	}
	p.writeBeforeComments(module)
	p.writef("module %s;", module.Identifier)
	p.writeRightComments(module)
	p.nl()
}

// writeImportedFiles identifies blocks of imports, sorts and writes them.
func (p *printer) writeImportedFiles(imports []*core.ImportedFile) {
	blockStart := 0
	for i, imported := range imports {
		if isNewBlock(imported) {
			p.writeImportedFilesBlock(imports[blockStart:i])
			blockStart = i
			p.nl()
		}
	}
	p.writeImportedFilesBlock(imports[blockStart:len(imports)])
}

// writeImportedFilesBlock sorts and writes a slice of import statements.
func (p *printer) writeImportedFilesBlock(imports []*core.ImportedFile) {
	sortImportedFiles(imports)
	for _, imported := range imports {
		p.writeBeforeComments(imported)
		p.writef("import \"%v\";", imported.SpecifiedName)
		p.writeRightComments(imported)
		p.nl()
	}
}

// writeDeclaredObject writes a declared object's attributes, then its
// preceeding comments and finally the object itself.
func (p *printer) writeDeclaredObject(declaredObject core.DeclaredObject) {
	if isNewBlock(declaredObject) {
		p.nl()
	}
	p.writeDeclaredObjectAttributes(declaredObject)
	p.writeBeforeComments(declaredObject)
	switch o := declaredObject.(type) {
	case *core.MojomStruct:
		p.writeMojomStruct(o)
	case *core.StructField:
		p.writeStructField(o)
	case *core.MojomUnion:
		p.writeMojomUnion(o)
	case *core.UnionField:
		p.writeUnionField(o)
	case *core.MojomEnum:
		p.writeMojomEnum(o)
	case *core.EnumValue:
		p.writeEnumValue(o)
	case *core.UserDefinedConstant:
		p.writeUserDefinedConstant(o)
	case *core.MojomInterface:
		p.writeMojomInterface(o)
	case *core.MojomMethod:
		p.writeMojomMethod(o)
	default:
		panic(fmt.Sprintf("writeDeclaredObject cannot write %v.", declaredObject))
	}
}

func (p *printer) writeMojomStruct(mojomStruct *core.MojomStruct) {
	p.write("struct ")
	p.writeDeclaredObjectsContainer(mojomStruct)
}

func (p *printer) writeMojomUnion(mojomUnion *core.MojomUnion) {
	p.write("union ")
	p.writeDeclaredObjectsContainer(mojomUnion)
}

func (p *printer) writeMojomEnum(mojomEnum *core.MojomEnum) {
	p.write("enum ")
	p.writeDeclaredObjectsContainer(mojomEnum)
}

func (p *printer) writeMojomInterface(mojomInterface *core.MojomInterface) {
	p.write("interface ")
	p.writeDeclaredObjectsContainer(mojomInterface)
}

func (p *printer) writeMojomMethod(mojomMethod *core.MojomMethod) {
	splitResponse := false
	if p.maxLineLength > 0 {
		scratch := newPrinter()
		scratch.maxLineLength = -1
		scratch.writeMojomMethod(mojomMethod)
		if len(scratch.result())+p.lineLength() > p.maxLineLength {
			splitResponse = true
		}
	}

	p.write(mojomMethod.NameToken().Text)
	if mojomMethod.DeclaredOrdinal() >= 0 {
		p.writef("@%v", mojomMethod.DeclaredOrdinal())
	}

	p.writeMethodParams(mojomMethod.Parameters, mojomMethod)
	if mojomMethod.ResponseParameters != nil {
		if splitResponse {
			p.nl()
			p.write("   ")
		}
		p.write(" => ")
		p.writeMethodParams(mojomMethod.ResponseParameters, mojomMethod)
	}
	p.write(";")
	p.writeRightComments(mojomMethod)
}

// writeMethodParams writes the pretty-printed method parameters represented by
// a MojomStruct.
func (p *printer) writeMethodParams(params *core.MojomStruct, method *core.MojomMethod) {
	// The presence or absence of a semi-colon needs to be taken into account when
	// trying to enforce the line size limit.
	semiColon := false
	if params.StructType() == core.StructTypeSyntheticResponse || method.ResponseParameters == nil {
		semiColon = true
	}
	ownLine := false
	if p.maxLineLength > 0 {
		scratch := newPrinter()
		scratch.maxLineLength = -1
		scratch.writeMethodParams(params, method)
		scratchLineLen := len(scratch.result()) + p.lineLength()
		// If a semi-colon will be added after the parameters, account for it.
		if semiColon {
			scratchLineLen += 1
		}
		if scratchLineLen > p.maxLineLength {
			ownLine = true
		}
	}
	p.write("(")
	declaredObjects := params.GetDeclaredObjects()

	extraIndent := p.lineLength() - p.indentSize
	if ownLine {
		// If we are printing every parameter on its own line, check to see if
		// aligning the parameters to the right of the method name can be done.
		for i, param := range declaredObjects {
			scratch := newPrinter()
			scratch.maxLineLength = -1
			scratch.writeMethodParam(param.(*core.StructField))
			// If any parameter would go beyond the maximum line length, start the
			// list of parameters on the line after the method name and indented 4
			// more than the method itself.
			scratchLineLen := len(scratch.result()) + p.lineLength()
			// If this is the last parameter, account for the closing parenthesis.
			if i == len(declaredObjects)-1 {
				scratchLineLen += 1
				// If a semi-colo will be added after the parameters, account for it.
				if semiColon {
					scratchLineLen += 1
				}
			}
			if scratchLineLen > p.maxLineLength {
				p.nl()
				extraIndent = 4
				break
			}
		}

		p.indentSize += extraIndent
	}

	for i, param := range declaredObjects {
		p.writeMethodParam(param.(*core.StructField))
		if i < len(declaredObjects)-1 {
			p.write(",")
			if ownLine {
				p.nl()
			} else {
				p.write(" ")
			}
		}
	}

	if ownLine {
		p.indentSize -= extraIndent
	}
	p.write(")")
}

// writeMethodParam writes a single pretty-printed method parameter represented
// by a StructField.
func (p *printer) writeMethodParam(param *core.StructField) {
	p.writeAttributesSingleLine(param.Attributes())
	p.writeBeforeComments(param)
	p.writeTypeRef(param.FieldType)
	p.writef(" %v", param.NameToken().Text)
	if param.DeclaredOrdinal() >= 0 {
		p.writef("@%v", param.DeclaredOrdinal())
	}
	if param.DefaultValue != nil {
		p.write("=")
		p.writeValueRef(param.DefaultValue)
	}
	p.writeRightComments(param)
}

func (p *printer) writeUserDefinedConstant(constant *core.UserDefinedConstant) {
	p.write("const ")
	p.writeTypeRef(constant.DeclaredType())
	p.writef(" %s", constant.NameToken().Text)
	p.write(" = ")
	p.writeValueRef(constant.ValueRef())
	p.write(";")
	p.writeRightComments(constant)
}

func (p *printer) writeStructField(structField *core.StructField) {
	p.writeTypeRef(structField.FieldType)
	p.writef(" %v", structField.NameToken().Text)
	if structField.DeclaredOrdinal() >= 0 {
		p.writef("@%v", structField.DeclaredOrdinal())
	}
	if structField.DefaultValue != nil {
		p.write(" = ")
		p.writeValueRef(structField.DefaultValue)
	}
	p.write(";")
	p.writeRightComments(structField)
}

func (p *printer) writeUnionField(unionField *core.UnionField) {
	p.writeTypeRef(unionField.FieldType)
	p.writef(" %v", unionField.NameToken().Text)
	if unionField.DeclaredOrdinal() >= 0 {
		p.writef("@%v", unionField.DeclaredOrdinal())
	}
	p.write(";")
	p.writeRightComments(unionField)
}

func (p *printer) writeEnumValue(enumValue *core.EnumValue) {
	p.write(enumValue.NameToken().Text)
	if enumValue.ValueRef() != nil {
		p.write(" = ")
		p.writeValueRef(enumValue.ValueRef())
	}
	p.write(",")
	p.writeRightComments(enumValue)
}

// writeDeclaredObjectsContainer
// DeclaredObjectsContainers are MojomEnum, MojomUnion, MojomInterface, MojomStruct.
// This method writes the name of the object, an opening brace, the contained
// declarations (fields, enum values, nested declarations and methods) and the
// closing brace.
func (p *printer) writeDeclaredObjectsContainer(container core.DeclaredObjectsContainer) {

	p.writef("%v {", container.(core.DeclaredObject).NameToken().Text)
	if len(container.GetDeclaredObjects()) == 0 && !containsFinalComments(container.(core.MojomElement)) {
		p.writef("};")
		return
	}
	p.writeRightComments(container.(core.MojomElement))
	p.incIndent()
	p.nl()
	declaredObjects := container.GetDeclaredObjects()
	for i, declaredObject := range declaredObjects {
		if i == 0 {
			trimEmptyLinesDeclaredObject(declaredObject)
		}
		p.writeDeclaredObject(declaredObject)
		if i < len(declaredObjects)-1 {
			p.nl()
		}
	}

	// Write the comments at the end of the struct, enum, union or interface body.
	p.writeFinalComments(container)

	p.decIndent()
	p.nl()
	p.write("};")
}

func (p *printer) writeDeclaredObjectAttributes(declaredObject core.DeclaredObject) {
	switch declaredObject.(type) {
	case *core.MojomStruct:
		p.writeAttributes(declaredObject.Attributes())
	case *core.MojomUnion:
		p.writeAttributes(declaredObject.Attributes())
	case *core.MojomEnum:
		p.writeAttributes(declaredObject.Attributes())
	case *core.UserDefinedConstant:
		p.writeAttributes(declaredObject.Attributes())
	case *core.MojomInterface:
		p.writeAttributes(declaredObject.Attributes())
	case *core.MojomMethod:
		p.writeAttributes(declaredObject.Attributes())
	case *core.StructField:
		p.writeAttributesSingleLine(declaredObject.Attributes())
	case *core.UnionField:
		p.writeAttributesSingleLine(declaredObject.Attributes())
	case *core.EnumValue:
		p.writeAttributesSingleLine(declaredObject.Attributes())
	default:
		panic(fmt.Sprintf("writeDeclaredObjectAttributes cannot write %v.", declaredObject))
	}
}

// See writeAttributesBase.
func (p *printer) writeAttributes(attrs *core.Attributes) {
	p.writeAttributesBase(attrs, false)
}

// writeAttributesSingleLine writes the provided attributes on a single line.
// This is used by the writeMethodParam to write the attributes of a parameter.
func (p *printer) writeAttributesSingleLine(attrs *core.Attributes) {
	p.writeAttributesBase(attrs, true)
}

// writeAttributesBase writes the provided attributes. If singleLine is
// false, every attribute is written on a new line and a new line is appended
// after the attributes are written. Otherwise, the attributes are written on
// a single line.
func (p *printer) writeAttributesBase(attrs *core.Attributes, singleLine bool) {
	if attrs == nil {
		return
	}

	p.writeBeforeComments(attrs)

	if len(attrs.List) == 0 {
		return
	}

	p.writef("[")
	for idx, attr := range attrs.List {
		p.writeAttribute(&attr)
		if idx < len(attrs.List)-1 {
			p.writef(",")
			if !singleLine {
				p.nl()
			}
			p.write(" ")
		}
	}
	p.writef("]")
	if singleLine {
		p.write(" ")
	} else {
		p.nl()
	}
}

func (p *printer) writeAttribute(attr *core.MojomAttribute) {
	p.writeBeforeComments(attr)
	p.writef("%s=", attr.Key)
	p.writeValueRef(attr.Value)
	p.writeRightComments(attr)
}

func (p *printer) writeTypeRef(t core.TypeRef) {
	switch t.TypeRefKind() {
	case core.TypeKindUserDefined:
		u := t.(*core.UserTypeRef)
		p.write(u.Identifier())
		if u.IsInterfaceRequest() {
			p.write("&")
		}
		if u.Nullable() {
			p.write("?")
		}
	case core.TypeKindArray:
		a := t.(*core.ArrayTypeRef)
		p.write("array<")
		p.writeTypeRef(a.ElementType())
		if a.FixedLength() >= 0 {
			p.writef(", %v", a.FixedLength())
		}
		p.write(">")
		if a.Nullable() {
			p.write("?")
		}
	case core.TypeKindMap:
		m := t.(*core.MapTypeRef)
		p.write("map<")
		p.writeTypeRef(m.KeyType())
		p.write(", ")
		p.writeTypeRef(m.ValueType())
		p.write(">")
		if m.Nullable() {
			p.write("?")
		}
	case core.TypeKindHandle:
		fallthrough
	case core.TypeKindSimple:
		fallthrough
	case core.TypeKindString:
		p.write(t.String())
	default:
		panic("This unhandled TypeRefKind: " + t.String())
	}
}

func (p *printer) writeValueRef(value core.ValueRef) {
	switch v := value.(type) {
	case core.LiteralValue:
		// The sign of a numeric literal is a separate token from the number itself.
		// So the token that is stored does not include a sign. Here we check if
		// the literal is a signed constant and if so, we print a negative sign if
		// appropriate.
		if _, ok := v.ValueType().(core.LiteralType); ok {
			negative := false
			switch num := v.Value().(type) {
			case int8:
				negative = num < 0
			case int16:
				negative = num < 0
			case int32:
				negative = num < 0
			case int64:
				negative = num < 0
			case float32:
				negative = num < 0
			case float64:
				negative = num < 0
			}
			if negative {
				p.write("-")
			}
		}
		if v.Token() == nil {
			panic(fmt.Sprintf("nil token when trying to print %s.", v.String()))
		}
		p.write(v.Token().Text)
	case *core.UserValueRef:
		p.write(v.Identifier())
	default:
		panic("Cannot handle this value ref.")
	}
}

// writeBeforeComments writes the comments preceeding a MojomElement.
func (p *printer) writeBeforeComments(el core.MojomElement) {
	attachedComments := el.AttachedComments()
	if attachedComments == nil {
		return
	}

	p.writeAboveComments(el)
	p.writeLeftComments(el)
}

// writeAboveComments writes the comments above of a MojomElement.
func (p *printer) writeAboveComments(el core.MojomElement) {
	attachedComments := el.AttachedComments()
	if attachedComments == nil {
		return
	}

	p.writeCommentBlocks(attachedComments.Above, true)
}

func (p *printer) writeFinalComments(container core.DeclaredObjectsContainer) {
	el := container.(core.MojomElement)
	attachedComments := el.AttachedComments()
	if attachedComments == nil {
		return
	}
	finalComments := trimEmptyLinesEnd(attachedComments.Final)
	if len(finalComments) == 0 {
		return
	}

	// Only print blank lines if there is something other than comments in the
	// container.
	if len(container.GetDeclaredObjects()) > 0 {
		p.nl()
		if finalComments[0].Kind == lexer.EmptyLine {
			p.nl()
		}
	}
	p.writeCommentBlocks(finalComments, false)
}

// writeLeftComments writes the comments left of a MojomElement.
func (p *printer) writeLeftComments(el core.MojomElement) {
	attachedComments := el.AttachedComments()
	if attachedComments == nil {
		return
	}

	for _, comment := range attachedComments.Left {
		if comment.Kind == lexer.SingleLineComment {
			panic("SingleLineComment cannot be on the left of an element.")
		}

		p.writef("%s ", comment.Text)
	}
}

// writeRightComments writes the comments to the right of a MojomElement.
func (p *printer) writeRightComments(el core.MojomElement) {
	attachedComments := el.AttachedComments()
	if attachedComments == nil {
		return
	}

	for i, comment := range attachedComments.Right {
		if comment.Kind == lexer.SingleLineComment {
			if i < len(attachedComments.Right)-1 {
				panic("You can't have anything after a SingleLineComment!")
			}
			p.setEolComment(comment)
			break
		}
		p.writef(" %s", comment.Text)
	}
}

// writeCommentBlocks writes a slice of comments. If finalEol is true, a new
// line is written after all the comments are written.
func (p *printer) writeCommentBlocks(comments []lexer.Token, finalEol bool) {
	for i, comment := range comments {
		switch comment.Kind {
		case lexer.SingleLineComment:
			if comment.Text == "// no-format" {
				p.startNoFormat(comment)
			} else if comment.Text == "// end-no-format" {
				p.endNoFormat(comment)
			}
			p.writeSingleLineComment(comment)
			if finalEol || i < len(comments)-1 {
				p.nl()
			}
		case lexer.MultiLineComment:
			p.writeMultiLineComment(comment)
			if finalEol || i < len(comments)-1 {
				p.nl()
			}
		case lexer.EmptyLine:
			// We only use EmptyLine here to separate comment blocks. And we collapse
			// sequences of empty lines to a single empty line.
			if i != 0 && comments[i-1].Kind != lexer.EmptyLine {
				p.nl()
			}
		default:
			panic(fmt.Sprintf("%s is not a comment.", comment))
		}
	}
}

func (p *printer) writeSingleLineComment(comment lexer.Token) {
	if comment.Kind != lexer.SingleLineComment {
		panic(fmt.Sprintf("This is not a SingleLineComment: %s", comment))
	}
	commentText := comment.Text

	// We expect that the first 2 characters are // followed by a space or tab.
	// If the third character is not a space or tab, we insert a space.
	// There is an exception for three forward slashes which are allowed.
	if len(commentText) > 2 && !strings.ContainsAny(" \t/", commentText[2:3]) {
		commentText = "// " + commentText[2:]
	}
	p.write(commentText)
}

func (p *printer) writeMultiLineComment(comment lexer.Token) {
	if comment.Kind != lexer.MultiLineComment {
		panic(fmt.Sprintf("This is not a MultiLineComment: %s", comment))
	}

	lines := strings.Split(comment.Text, "\n")
	for i, line := range lines {
		trimmed := strings.Trim(line, " \t")
		// If a line starts with * we assume the user is trying to use the pattern
		// whereby they align the comment with spaces after the *. Otherwise,
		// we try to align the comment by prepending 3 spaces.
		if i != 0 && len(trimmed) > 0 {
			if trimmed[0] == '*' {
				p.write(" ")
			} else {
				p.write("   ")
			}
		}

		if p.lineLength()+len(trimmed) <= p.maxLineLength || p.maxLineLength < 0 {
			p.write(trimmed)
		} else {
			// If the comment is too long to fit it on a line, break up the line along
			// spaces and wrap the line.
			words := strings.Split(trimmed, " ")
			for i, word := range words {
				if i > 0 && len(word)+p.lineLength()+1 > p.maxLineLength {
					p.nl()
					if trimmed[0] == '*' {
						p.write(" *")
					} else {
						p.write("  ")
					}
				}
				if i > 0 {
					p.write(" ")
				}
				p.write(word)
			}
		}
		if i < len(lines)-1 {
			p.nl()
		}
	}
}

// Utility functions

// writef writes according to the format specifier. See fmt.Printf for the
// format specifier.
func (p *printer) writef(format string, a ...interface{}) {
	p.write(fmt.Sprintf(format, a...))
}

// write writes the provided string to the buffer.
// If nothing has been written yet on the current line write also writes the
// current indentation level.
func (p *printer) write(s string) {
	if strings.ContainsRune(s, '\n') {
		panic(fmt.Sprintf("Only the nl method can write a new line: %q", s))
	}

	// We only print the indentation if the line is not empty.
	if p.linePos == 0 {
		if !p.noFormat {
			p.buffer.WriteString(strings.Repeat(" ", p.indentSize))
		}
		p.linePos += p.indentSize
	}
	p.linePos += len(s)
	if !p.noFormat {
		p.buffer.WriteString(s)
	}
}

// nl writes a new line. Before writing the new line, nl writes the last
// comment on the line.
func (p *printer) nl() {
	// Before going to the next line, print the last comment on the line.
	if p.eolComment != nil {
		if !p.noFormat {
			p.write("  ")
			p.writeSingleLineComment(*p.eolComment)
		}
		p.eolComment = nil
	}

	if !p.noFormat {
		p.buffer.WriteString("\n")
	}
	p.linePos = 0
}

func (p *printer) incIndent() {
	p.indentSize += 2
}

func (p *printer) decIndent() {
	p.indentSize -= 2
	if p.indentSize < 0 {
		panic("The printer is attempting to use negative indentation!")
	}
}

// setEolComment sets the comment that is to be printed at the end of the current line.
// The last comment on the line is not necessarily associated with the last
// element on the line. So when the last comment is found, we record that
// comment so we may print it right before going to the next line.
func (p *printer) setEolComment(comment lexer.Token) {
	if p.eolComment != nil {
		panic("There is space for only one comment at the end of the line!")
	}

	if comment.Kind != lexer.SingleLineComment {
		panic("Only SingleLineComments need to be handled specially at the end of line.")
	}

	p.eolComment = &comment
}

// lineLength returns the length of the line so far. If nothing has been written
// to the line yet, it returns the indent size.
// The purpose of lineLenght is to answer the question: If I write something now
// how far on the current line will it be written?
func (p *printer) lineLength() int {
	if p.linePos == 0 {
		return p.indentSize
	}
	return p.linePos
}

// startNoFormat disables writes to the buffer (future writes are discarded).
func (p *printer) startNoFormat(startToken lexer.Token) {
	if p.noFormat {
		return
	}

	p.noFormat = true
	p.noFormatStart = startToken
	p.noFormatStartIndent = p.indentSize
}

// endNoFormat re-enables writes to the buffer and writes the source starting
// at the beginning of p.startToken and ending right before endToken.
func (p *printer) endNoFormat(endToken lexer.Token) {
	if !p.noFormat {
		return
	}

	fileContents := p.mojomFile.FileContents()
	notFormatted := fileContents[p.noFormatStart.SourcePosBytes:endToken.SourcePosBytes]

	// Remove any unformatted indentation right before the end of the unformatted block.
	notFormatted = strings.TrimRight(notFormatted, " \t")
	p.buffer.WriteString(strings.Repeat(" ", p.noFormatStartIndent))
	p.buffer.WriteString(notFormatted)
	p.noFormat = false
}

// eofNoFormat handles the case where formatting had been disabled but not
// re-enabled. It writes everything after the formatting was ended.
func (p *printer) eofNoFormat() {
	if !p.noFormat {
		return
	}

	fileContents := p.mojomFile.FileContents()
	p.endNoFormat(lexer.Token{SourcePosBytes: len(fileContents)})
}

// Standalone utilities that do not operate on the buffer.

// isNewBlock determines if an empty line should be printed above the element
// under consideration. The purpose is to respect user-intention to separate
// elements in a mojom file.
//
// If the element has attributes, and the first "comment" attached above the
// attributes is an empty line, the element is a new block.
// If the element has no attributes and the first "comment" attached above the
// element itself is an empty line, the element is a new block.
// Else, the element is not a new block.
func isNewBlock(mojomElement core.MojomElement) bool {
	if declaredObject, ok := mojomElement.(core.DeclaredObject); ok {
		if declaredObject.Attributes() != nil {
			comments := declaredObject.Attributes().AttachedComments()
			if comments == nil || len(comments.Above) == 0 {
				return false
			}
			return comments.Above[0].Kind == lexer.EmptyLine
		}
	}
	comments := mojomElement.AttachedComments()

	if comments == nil || len(comments.Above) == 0 {
		return false
	}
	return comments.Above[0].Kind == lexer.EmptyLine
}

// If the element has AttachedComments and there is at least one comment
// in AttachedComments.Final which is not an EmptyLine, containsFinalComments
// returns true. Otherwise, it returns false.
func containsFinalComments(el core.MojomElement) bool {
	attachedComments := el.AttachedComments()
	if attachedComments == nil {
		return false
	}

	for _, comment := range attachedComments.Final {
		if comment.Kind != lexer.EmptyLine {
			return true
		}
	}
	return false
}

// trimEmptyLinesEnd trims out empty line comments at the end of a slice of comments.
func trimEmptyLinesEnd(comments []lexer.Token) []lexer.Token {
	lastNonEmpty := -1
	for i, comment := range comments {
		if comment.Kind != lexer.EmptyLine {
			lastNonEmpty = i
		}
	}
	return comments[:lastNonEmpty+1]
}

// trimEmptyLinesBegin trims out empty line comments at the beginning of a slice of comments.
func trimEmptyLinesBegin(comments []lexer.Token) []lexer.Token {
	for i, comment := range comments {
		if comment.Kind != lexer.EmptyLine {
			return comments[i:]
		}
	}
	return comments[len(comments):]
}

// trimEmptyLinesDeclaredObject trims out empty lines from the beginning of the
// comments on a DeclaredObject or its Attributes.
func trimEmptyLinesDeclaredObject(declaredObject core.DeclaredObject) {
	if declaredObject.Attributes() != nil {
		comments := declaredObject.Attributes().AttachedComments()
		if comments == nil {
			return
		}
		comments.Above = trimEmptyLinesBegin(comments.Above)
		return
	}
	comments := declaredObject.AttachedComments()

	if comments == nil || len(comments.Above) == 0 {
		return
	}
	comments.Above = trimEmptyLinesBegin(comments.Above)
}

// Following is a utility to sort slices of |ImportedFile|s.

// sortImportedFiles sorts the slice of imported files it receives.
func sortImportedFiles(imports []*core.ImportedFile) {
	sort.Sort(&importedFilesSorter{imports})
}

type importedFilesSorter struct {
	imports []*core.ImportedFile
}

// See sort.Interface.
func (ifs *importedFilesSorter) Len() int {
	return len(ifs.imports)
}

// See sort.Interface.
func (ifs *importedFilesSorter) Less(i, j int) bool {
	return ifs.imports[i].SpecifiedName < ifs.imports[j].SpecifiedName
}

// See sort.Interface.
func (ifs *importedFilesSorter) Swap(i, j int) {
	ifs.imports[i], ifs.imports[j] = ifs.imports[j], ifs.imports[i]
}
