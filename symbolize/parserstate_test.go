package symbolize

import "testing"

func TestPeek(t *testing.T) {
	buf := ParserState("this is a test")
	if !buf.peek("this") {
		t.Error("'this' is at the start")
	}
	if buf.peek("bob") {
		t.Error("'bob' is not at the start")
	}
	if buf.peek("thistle") {
		t.Error("'thistle' is not at the start")
	}
}

func TestExpect(t *testing.T) {
	buf := ParserState("this is a test")
	if !buf.expect("this") {
		t.Error("'this' is at the start")
	}
	if string(buf) != " is a test" {
		t.Error("expected ", " is a test", "got", string(buf))
	}
	if buf.expect("is") {
		t.Error("a space is missing")
	}
	if !buf.expect(" is") || !buf.expect(" a ") {
		t.Error("something is wrong")
	}
	if string(buf) != "test" {
		t.Error("expected", "test", "got", string(buf))
	}
	buf.expect("blarg")
	if string(buf) != "test" {
		t.Error("input consumed when it should not have been")
	}
}

func TestBefore(t *testing.T) {
	buf := ParserState("this is a test")
	v1, err := buf.before(" ")
	if err != nil {
		t.Error(err)
	}
	if v1 != "this" {
		t.Error("expected", "this", "got", v1)
	}
	v2, err := buf.before(" ")
	if err != nil {
		t.Error(err)
	}
	if v2 != "is" {
		t.Error("expected", "is", "got", v2)
	}
	if string(buf) != "a test" {
		t.Error("expected", "a test", "got", string(buf))
	}
	_, err = buf.before("#")
	if err == nil {
		t.Error("expected an error but got none")
	}
	if string(buf) != "a test" {
		t.Error("input consumed when it should not have been")
	}
}

func TestDecBefore(t *testing.T) {
	buf := ParserState("10:020:0030;")
	v1, err1 := buf.decBefore(":")
	v2, err2 := buf.decBefore(":")
	v3, err3 := buf.decBefore(";")
	if err1 != nil || err2 != nil || err3 != nil {
		t.Error(err1, err2, err3)
	}
	if v1 != 10 || v2 != 20 || v3 != 30 {
		t.Error("expected", []uint64{10, 20, 30}, "got", []uint64{v1, v2, v3})
	}
	if len(buf) != 0 {
		t.Error("not all input was consumed")
	}
}

func TestIntBefore(t *testing.T) {
	buf := ParserState("10:0x20:30;")
	v1, err1 := buf.intBefore(":")
	v2, err2 := buf.intBefore(":")
	v3, err3 := buf.intBefore(";")
	if err1 != nil || err2 != nil || err3 != nil {
		t.Error(err1, err2, err3)
	}
	if v1 != 10 || v2 != 0x20 || v3 != 30 {
		t.Error("expected", []uint64{10, 0x20, 30}, "got", []uint64{v1, v2, v3})
	}
	if len(buf) != 0 {
		t.Error("not all input was consumed")
	}
}

func TestFloatBefore(t *testing.T) {
	// Note these floats are exactly represented.
	buf := ParserState("0.125:.25:300.50;")
	v1, err1 := buf.floatBefore(":")
	v2, err2 := buf.floatBefore(":")
	v3, err3 := buf.floatBefore(";")
	if err1 != nil || err2 != nil || err3 != nil {
		t.Error(err1, err2, err3)
	}
	if v1 != 0.125 || v2 != 0.25 || v3 != 300.5 {
		t.Error("expected", []float64{0.125, 0.25, 300.5}, "got", []float64{v1, v2, v3})
	}
	if len(buf) != 0 {
		t.Error("not all input was consumed")
	}
}

func TestTry(t *testing.T) {
	buf := ParserState("foo bar baz")
	buf.try(func(buf *ParserState) interface{} {
		buf.expect("foo")
		return nil
	})
	if string(buf) != "foo bar baz" {
		t.Error("try should not consume input on failure")
	}
	node := buf.try(func(buf *ParserState) interface{} {
		buf.expect("foo ")
		buf.before(" ")
		buf.before("z")
		return "dummy string"
	})
	if node == nil || string(buf) != "" {
		t.Error("try did not succeed when it should have")
	}
}

func TestPrefix(t *testing.T) {
	buf := ParserState("foo bar baz")
	node := buf.prefix("baz", func(buf *ParserState) interface{} {
		return ""
	})
	if node != nil || buf != "foo bar baz" {
		t.Error("prefix messed up")
	}
}
