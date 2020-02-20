package support

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func ZbiPath(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)

	return filepath.Join(exPath, "../zedboot.zbi")
}

func ToolPath(t *testing.T, name string) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "test_data", "tools", name)
}

func EnsureContains(t *testing.T, output string, lookFor string) {
	if !strings.Contains(output, lookFor) {
		t.Fatalf("output did not contain '%s'", lookFor)
	}
}

func EnsureDoesNotContain(t *testing.T, output string, lookFor string) {
	if strings.Contains(output, lookFor) {
		t.Fatalf("output contains '%s'", lookFor)
	}
}
