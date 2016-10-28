package parser

import (
	"fidl/compiler/cmd/fidl/parser"
	"fidl/compiler/core"
	"fidl/compiler/lexer"
	"testing"
)

func TestAttachComments(t *testing.T) {
	checkEq := func(expected, actual interface{}) {
		if expected != actual {
			t.Fatalf("Failed check: Expected (%v), Actual (%v)", expected, actual)
		}
	}

	source := `
	// TopComment

	// NextComment
	/* LeftAttrsComment */[Key1="SomeModule", // AttrsRightComment
	Key2=10, // Key2AttrRightComment
	Key3=12]
	module hello.world;

	import "import1";
	import "import2";

	interface InterfaceFoo {
		Method1(int32 in_param1 /* InParam1Comment */, int32 in_param2); // Method1RightComment
		// FinalInterfaceComment
	};

	// FinalComments
	`

	descriptor := core.NewMojomDescriptor()
	parser := parser.MakeParser("TestAttachComments", "TestAttachComments", source, descriptor, nil)
	parser.Parse()

	if !parser.OK() {
		t.Errorf("Parser was not supposed to fail: %v", parser.GetError().Error())
	}

	mojomFile := parser.GetMojomFile()
	comments := parser.GetComments()

	core.AttachCommentsToMojomFile(mojomFile, comments)

	topAttrComments := mojomFile.Attributes.AttachedComments()
	checkEq(lexer.EmptyLine, topAttrComments.Above[0].Kind)
	checkEq("// TopComment", topAttrComments.Above[1].Text)
	checkEq(lexer.EmptyLine, topAttrComments.Above[2].Kind)
	checkEq("// NextComment", topAttrComments.Above[3].Text)
	checkEq("/* LeftAttrsComment */", topAttrComments.Left[0].Text)
	checkEq("// AttrsRightComment", topAttrComments.Right[0].Text)

	key2AttrComments := mojomFile.Attributes.List[1].AttachedComments()
	checkEq("// Key2AttrRightComment", key2AttrComments.Right[0].Text)

	interfaceFoo := mojomFile.DeclaredObjects[0].(*core.MojomInterface)
	// Sanity-check that we got the right object.
	checkEq("InterfaceFoo", interfaceFoo.SimpleName())
	checkEq("// FinalInterfaceComment", interfaceFoo.AttachedComments().Final[0].Text)

	method1 := interfaceFoo.MethodsByOrdinal[0]
	// Sanity-check that we got the right method.
	checkEq("Method1", method1.SimpleName())

	inParam1 := method1.Parameters.FieldsInLexicalOrder[0]
	// Sanity-check that we got the right field.
	checkEq("in_param1", inParam1.SimpleName())

	checkEq("// Method1RightComment", method1.AttachedComments().Right[0].Text)
	checkEq("/* InParam1Comment */", inParam1.AttachedComments().Right[0].Text)

	checkEq(lexer.EmptyLine, mojomFile.FinalComments[0].Kind)
	checkEq("// FinalComments", mojomFile.FinalComments[1].Text)
}

func TestEmptyFile(t *testing.T) {
	descriptor := core.NewMojomDescriptor()
	parser := parser.MakeParser("TestEmptyFile", "TestEmptyFile", "", descriptor, nil)
	parser.Parse()

	mojomFile := parser.GetMojomFile()
	comments := parser.GetComments()

	core.AttachCommentsToMojomFile(mojomFile, comments)
}

func TestNoComments(t *testing.T) {
	source := `
	module hello.world;

	interface InterfaceFoo {
		Method1(int32 in_param1);
	};
	`
	descriptor := core.NewMojomDescriptor()
	parser := parser.MakeParser("TestNoComments", "TestNoComments", source, descriptor, nil)
	parser.Parse()

	mojomFile := parser.GetMojomFile()
	comments := parser.GetComments()

	core.AttachCommentsToMojomFile(mojomFile, comments)
}

func TestOnlyComments(t *testing.T) {
	source := `
	// Hello world of comments
			`
	descriptor := core.NewMojomDescriptor()
	parser := parser.MakeParser("TestOnlyComments", "TestOnlyComments", source, descriptor, nil)
	parser.Parse()

	mojomFile := parser.GetMojomFile()
	comments := parser.GetComments()

	core.AttachCommentsToMojomFile(mojomFile, comments)
}
