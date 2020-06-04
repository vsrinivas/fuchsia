package lib

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"regexp"
)

type License struct {
	pattern  *regexp.Regexp
	matches  []Match
	category string
}

type Licenses struct {
	licenses []License
}

func LicenseWorker(path string) *[]byte {
	licensePatternFile, _ := os.Open(path)
	defer licensePatternFile.Close()
	bytes, err := ioutil.ReadAll(licensePatternFile)
	if err != nil {
		panic(err)
	}
	return &bytes
}

func (licenses *Licenses) Init(root string) {
	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if info.IsDir() {
			return nil
		}
		bytes := LicenseWorker(path)
		str := string(*bytes)
		fmt.Println(str)
		fmt.Println(root)
		licenses.add(License{
			pattern:  regexp.MustCompile(str),
			category: info.Name(),
		})
		return nil
	})
	if err != nil {
		panic(err)
	}
	if len(licenses.licenses) == 0 {
		panic("no licenses")
	}
}

func (licenses *Licenses) add(license License) {
	licenses.licenses = append(licenses.licenses, license)
}

type Match struct {
	value []byte
	files []string
}
