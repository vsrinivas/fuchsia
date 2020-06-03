package licenses

import (
	"fmt"
	"os"
	"path/filepath"
)

type Config struct {
	filesRegex     []string
	skipFilesRegex []string
	skipDirsRegex  []string
}

func Walk(root string, config Config) error {
	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			fmt.Printf("prevent panic by handling failure accessing a path %q: %v\n", path, err)
			return err
		}
		if info.IsDir() && info.Name() == ".git" {
			fmt.Printf("skipping a dir without errors: %+v \n", info.Name())
			return filepath.SkipDir
		}
		fmt.Printf("visited file or dir: %q\n", path)
		return nil
	})

	if err != nil {
		fmt.Printf("error walking the path %q: %v\n", root, err)
		return nil
	}
	return nil
}
