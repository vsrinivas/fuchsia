package logger

import (
	"context"
	goLog "log"
	"os"
	"testing"

	"fuchsia.googlesource.com/tools/color"
)

func TestWithContext(t *testing.T) {
	logger := NewLogger(DebugLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr)
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
	logger := NewLogger(InfoLevel, color.NewColor(color.ColorAuto), nil, nil)
	logFlags, errFlags := logger.goLogger.Flags(), logger.goErrorLogger.Flags()

	if logFlags != goLog.LstdFlags || errFlags != goLog.LstdFlags {
		t.Fatalf("New loggers should have the proper flags set for both standard and error logging. Expected: \n%+v and %+v\n but got: \n%+v and %+v", goLog.LstdFlags, goLog.LstdFlags, logFlags, errFlags)
	}

}
