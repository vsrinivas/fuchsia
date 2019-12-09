package symbolize

import (
	"testing"
)

func TestRestartIfNeeded(t *testing.T) {
	oldRestartSymbolizer := restartSymbolizer
	defer func() {
		restartSymbolizer = oldRestartSymbolizer
	}()
	restartSymbolizer = func(s *LLVMSymbolizer) error {
		return nil
	}
	s := NewLLVMSymbolizer("/dev/null", 0)
	for i := 0; i < 5; i++ {
		if s.restartIfNeeded() {
			t.Error("restartInterval := 0 should mean restartIfNeeded always returns false")
		}
	}
	s = NewLLVMSymbolizer("/dev/null", 1)
	for i := 0; i < 5; i++ {
		if !s.restartIfNeeded() {
			t.Error("restartInterval := 1 should mean restartIfNeeded always returns true")
		}
	}
	s = NewLLVMSymbolizer("/dev/null", 2)
	for i := 0; i < 5; i++ {
		if s.restartIfNeeded() != (i%2 == 1) {
			t.Error("restartInterval := 2 should mean restartIfNeeded returns true every other time")
		}
	}
}
