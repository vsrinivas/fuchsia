// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parser

import (
	"fmt"
	"fidl/compiler/lexer"
	"fidl/compiler/core"
	"strings"
)

///////////////////////////////////////////////////////////////////////
/// Type Parser
/// //////////////////////////////////////////////////////////////////

// This file contains the definition of the Parser type but it does not contain
// the actual parsing logic. That may be found in parsing.go

// A Parser is constructed and used to parse a single mojom file. The
// Parser is given a pointer to a MojomDescriptor that it will populate.
// The same MojomDescriptor may be given to successive runs of the Parser
// so that an entire graph of .mojom files may be parsed.
type Parser struct {
	// The stream of input tokens
	inputStream lexer.TokenStream

	// The current error state. In the current generation of the Parser we
	// only handle a single parse error before giving up. If an error
	// has been encountered then |err| is not nil.
	// TODO(rudominer) Enhancement: Parser should be able to keep going
	// after some errors. Change this field to be a list of errors instead of
	// a single error.
	err error

	// Last token seen, whether or not it was consumed.
	lastSeen lexer.Token

	// Last token consumed. May or may not be equal to lastSeen.
	lastConsumed lexer.Token

	// The root of the parse tree being constructed. This may be nil
	// because the parse tree is only explicitly constructed in
	// debug mode.
	rootNode *ParseNode

	// The current node of the parse tree in our recursive descent. This
	// may be nil because the parse tree is only explicitly constructed fo
	// in debug mode.
	currentNode *ParseNode

	// The MojomDescriptor being filled in by the Parser. This is passed
	// in in the constructor.
	mojomDescriptor *core.MojomDescriptor

	// The Parser creates a new instance of MojomFile on each call to Parse(),
	// which may only be called once per instance.
	mojomFile *core.MojomFile

	// The top of the Scope stack.
	currentScope *core.Scope

	debugMode bool
	used      bool

	// In meta-data-only mode the parser will parse the module statement,
	// the import statements and the file attributes and then return without
	// parsing any of the mojom declarations. The result is as if the
	// .mojom file did not have any declarations.
	metaDataOnlyMode bool

	// Set this variable to true to discard all remaining tokens in the
	// |inputStream|.
	discardRemaining bool
}

// Make a new Parser in preparation for calling Parse().
func MakeParser(canonicalFileName, specifiedName, fileContents string,
	descriptorToPopulate *core.MojomDescriptor, importedFrom *core.MojomFile) Parser {
	if descriptorToPopulate == nil {
		panic("descriptorToPopulate must not be nil")
	}
	inputStream := lexer.Tokenize(fileContents)
	parser := Parser{inputStream: inputStream,
		mojomDescriptor: descriptorToPopulate}
	parser.mojomDescriptor = descriptorToPopulate
	parser.mojomFile = parser.mojomDescriptor.AddMojomFile(canonicalFileName, specifiedName,
		importedFrom, fileContents)
	return parser
}

func (p *Parser) SetDebugMode(debug bool) {
	p.debugMode = debug
}

func (p *Parser) SetMetaDataOnlyMode(metaDataOnly bool) {
	p.metaDataOnlyMode = metaDataOnly
}

// Perform the parsing on the |fileContents| passed to MakeParser().
// The |descriptorToPopulate| passed to MakeParser() will be populated.
// After Parse() is done call GetMojomFile() to get the resulting
// |MojomFile|. The |Imports| field of that |MojomFile| gives the
// files imported by the file that was just parsed. For each file |f|
// in |Imports|, call MojomDescriptor.ContainsFile(f) on
// |descriptorToPopulate| to determine whether or not |f| has already
// been parsed. If not then construct another Parser for |f| and its
// contents and call Parse() again.
func (p *Parser) Parse() {
	if p.used {
		panic("An instance of Parser may only be used once.")
	}
	p.used = true

	// Perform the recursive descent.
	p.parseMojomFile()

	// Check if there are any extraneous tokens left in the stream.
	if p.OK() && !p.checkEOF() {
		token := p.peekNextToken("")
		message := fmt.Sprintf("Extraneous token: %v.", token)
		p.parseErrorT(ParserErrorCodeExtraneousToken, message, token)
	}
}

// After Parse() is done call this method to obtain the resulting
// MojomFile.
func (p *Parser) GetMojomFile() *core.MojomFile {
	return p.mojomFile
}

// After Parse() is done call this method to obtain the comment tokens that were
// filtered out.
func (p *Parser) GetComments() []lexer.Token {
	return p.inputStream.(*lexer.FilteredTokenStream).FilteredTokens()
}

// Returns the root of the parse tree if this Parser is in debug mode.
// Otherwise returns nil.
func (p *Parser) GetParseTree() *ParseNode {
	return p.rootNode
}

////////////////////////////////////////////////////////////////////////////
// Parse Error Handling
////////////////////////////////////////////////////////////////////////////

type ParseError struct {
	code    ParseErrorCode
	file    *core.MojomFile
	token   lexer.Token
	message string
}

// Make ParseError implement the error interface.
func (e ParseError) Error() string {
	return core.UserErrorMessage(e.file, e.token, e.message)
}

// parseError sets the parser's current error to a ParseError with the given data.
func (p *Parser) parseError(code ParseErrorCode, message string) {
	p.parseErrorT(code, message, p.lastSeen)
}

// parseErrorT sets the parser's current error to a ParseError with the given data.
func (p *Parser) parseErrorT(code ParseErrorCode, message string, token lexer.Token) {
	p.err = &ParseError{
		code:    code,
		file:    p.mojomFile,
		token:   token,
		message: message,
	}
}

// Returns whether or not the Parser is in a non-error state.
func (p *Parser) OK() bool {
	return p.err == nil
}

// Returns the current ParseError or nil if OK() is true.
func (p *Parser) GetError() error {
	return p.err
}

//////////// Error codes //////////
type ParseErrorCode int

const (
	// An attributes section appeared in a location it is not allowed.
	ParserErrorCodeBadAttributeLocation ParseErrorCode = iota

	// Unexpected end-of-file
	ParserErrorCodeEOF

	// A simple name was expected but an identifier contained a dot.
	ParserErrorCodeExpectedSimpleName

	// After what appears to be a complete mojom file there were extra tokens.
	ParserErrorCodeExtraneousToken

	// An integer literal value was too large
	ParserErrorCodeIntegerOutOfRange

	// An integer literal value was ill-formed.
	// TODO(azani) This is only necessary because the lexer allows some
	// illegal tokens such as "0x"
	ParserErrorCodeIntegerParseError

	// A semicolon was missing.
	// TODO(rudominer) Consider elimintating most semicolons from the language.
	ParserErrorCodeMissingSemi

	// The type of a value is not compatible with the type of the variable
	// to which it is being assigned.
	ParserErrorCodeNotAssignmentCompatible

	// Either an explicitly specified ordinal is out of range, or else the
	// combination of explicitly specified ordinals is inconsistent.
	ParserErrorCodeBadOrdinal

	// An unexpected token was encountered. This is the most common error.
	ParserErrorCodeUnexpectedToken
)

////////////////////////////////////////////////////////////////////////////
// Methods for accessing the stream of tokens.
////////////////////////////////////////////////////////////////////////////

// Returns the next available token in the stream without advancing the
// stream cursor. In case the stream cursor is already past the end
// the returned Token will be the EOF token. In this case the global
// error state will be set to ParserErrorCodeEOF error code with the message
// "Unexpected end-of-file " concatenated with |eofMessage|. In case of
// any other type of error the returned token is unspecified and the
// global error state will be set with more details.
func (p *Parser) peekNextToken(eofMessage string) (nextToken lexer.Token) {
	if p.discardRemaining {
		nextToken = lexer.EofToken()
	} else {
		nextToken = p.inputStream.PeekNext()
	}
	if nextToken.EOF() {
		errorMessage := "Unexpected end-of-file. " + eofMessage
		p.parseError(ParserErrorCodeEOF, errorMessage)
	}
	p.lastSeen = nextToken
	return
}

// This method is similar to peekNextToken except that in the case of EOF
// it does not set the global error state but rather returns |eof| = |true|.
// This method is useful when EOF is an allowed state and you want
// to know what the extraneous token is in case it is not EOF.
func (p *Parser) checkEOF() (eof bool) {
	if p.discardRemaining {
		p.lastSeen = lexer.EofToken()
	} else {
		p.lastSeen = p.inputStream.PeekNext()
	}
	eof = p.lastSeen.EOF()
	return
}

// Sets p.lastConsumed to the value of the next available token in the
// stream and then advances the stream cursor. If the cursor is already
// past the end of the stream then it sets p.lastConsumed to the EOF
// token.
func (p *Parser) consumeNextToken() {
	if p.discardRemaining {
		p.lastSeen = lexer.EofToken()
		p.lastConsumed = p.lastSeen
		return
	}
	p.lastConsumed = p.inputStream.PeekNext()
	p.inputStream.ConsumeNext()
}

////////////////////////////////////////////////////////////////////////////
// Parse Tree Support
////////////////////////////////////////////////////////////////////////////

// In normal operation we do not explicit construct a parse tree. This is
// only used in debug mode.

///// ParseNode type /////
type ParseNode struct {
	name     string
	tokens   []*lexer.Token
	parent   *ParseNode
	children []*ParseNode
}

func (node *ParseNode) String() string {
	return toString(node, 0)
}

// Recursively generates a string representing a tree of nodes
// where indentLevel indicates the level in the tree
func toString(node *ParseNode, indentLevel int) string {
	prefix := "\n" + strings.Repeat(".", indentLevel) + "^"
	firstTokens := ""
	if node.tokens != nil {
		firstTokens = fmt.Sprintf("%s", node.tokens)
	}
	s := fmt.Sprintf("%s%s%s", prefix, node.name, firstTokens)
	if node.children != nil {
		for _, child := range node.children {
			s += toString(child, indentLevel+3)
		}
	}
	return s
}

func newParseNode(name string) *ParseNode {
	node := new(ParseNode)
	node.name = name
	return node
}

func (node *ParseNode) appendChild(name string, firstToken *lexer.Token) *ParseNode {
	child := newParseNode(name)
	child.tokens = append(child.tokens, firstToken)
	child.parent = node
	node.children = append(node.children, child)
	return child
}

func (p *Parser) pushRootNode(name string) {
	if !p.debugMode {
		return
	}
	p.rootNode = newParseNode(name)
	p.currentNode = p.rootNode
}

func (p *Parser) pushChildNode(name string) {
	if !p.debugMode {
		return
	}
	if p.currentNode == nil {
		panic("pushRootNode() must be invoked first.")
	}
	tokenCopy := p.lastSeen
	childNode := p.currentNode.appendChild(name, &(tokenCopy))
	p.currentNode = childNode
}

func (p *Parser) attachToken() {
	if !p.debugMode {
		return
	}
	if p.currentNode == nil {
		panic("Stack is empty.")
	}
	tokenCopy := p.lastSeen
	p.currentNode.tokens = append(p.currentNode.tokens, &tokenCopy)
}

func (p *Parser) popNode() {
	if !p.debugMode {
		return
	}
	if p.currentNode == nil {
		panic("stack is empty.")
	}
	p.currentNode = p.currentNode.parent
}
