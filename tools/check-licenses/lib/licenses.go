package lib

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"regexp"
	"strings"
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
	// TODO consider async although may not be needed due to sequential file access
	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		// TODO maybe don't search root recursively
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

type LicenseFileTree struct {
	name     string
	parent   *LicenseFileTree
	children map[string]*LicenseFileTree
	files    []string
}

func (license_file_tree *LicenseFileTree) Init() {
	license_file_tree.children = make(map[string]*LicenseFileTree)
}

func (license_file_tree *LicenseFileTree) upsert(path string, files []string) {
	pieces := strings.Split(path, "/")
	curr := license_file_tree
	// TODO parent isn't currently being used, so it can be removed
	var parent *LicenseFileTree
	for _, piece := range pieces {
		if _, found := curr.children[piece]; !found {
			// TODO default constructor on single line
			curr.children[piece] = new(LicenseFileTree)
			curr.children[piece].Init()
			curr.children[piece].name = piece
			curr.children[piece].parent = parent
		}
		parent = curr
		curr = curr.children[piece]
	}
	curr.files = files
}

func NewLicenses(root string) *Licenses {
	licenses := Licenses{}
	licenses.Init(root)
	return &licenses
}
