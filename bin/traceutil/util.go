package main

import (
	"fmt"
	"os/exec"
	"path"
)

func runCommand(command string, args ...string) error {
	cmd := exec.Command(command, args...)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("%s failed.  Output:\n%s", command, output)
	}
	return nil
}

func getCommandOutput(command string, args ...string) (string, error) {
	cmd := exec.Command(command, args...)
	output, err := cmd.CombinedOutput()
	return string(output), err
}

func replaceFilenameExt(filename string, newExt string) string {
	oldExt := path.Ext(filename)
	return filename[0:len(filename)-len(oldExt)] + "." + newExt
}
