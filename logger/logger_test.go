package logger

import (
	"context"
	goLog "log"
	"os"
	"testing"

	"go.fuchsia.dev/tools/color"
)

func TestWithContext(t *testing.T) {
	logger := NewLogger(DebugLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "")
	ctx := context.Background()
	if v, ok := ctx.Value(globalLoggerKeyType{}).(*Logger); ok || v != nil {
		t.Fatalf("Default context should not have globalLoggerKeyType. Expected: \nnil\n but got: \n%+v ", v)
	}
	ctx = WithLogger(ctx, logger)
	if v, ok := ctx.Value(globalLoggerKeyType{}).(*Logger); !ok || v == nil {
		t.Fatalf("Updated context should have globalLoggerKeyType, but got nil")
	}

}

func TestNewLogger(t *testing.T) {
	prefix := "testprefix "

	logger := NewLogger(InfoLevel, color.NewColor(color.ColorAuto), nil, nil, prefix)
	logFlags, errFlags := logger.goLogger.Flags(), logger.goErrorLogger.Flags()

	if logFlags != goLog.LstdFlags || errFlags != goLog.LstdFlags {
		t.Fatalf("New loggers should have the proper flags set for both standard and error logging. Expected: \n%+v and %+v\n but got: \n%+v and %+v", goLog.LstdFlags, goLog.LstdFlags, logFlags, errFlags)
	}

	logPrefix := logger.prefix
	if logPrefix != prefix {
		t.Fatalf("New loggers should use the specified prefix on creation. Expected: \n%+v\n but got: \n%+v", prefix, logPrefix)
	}
}
