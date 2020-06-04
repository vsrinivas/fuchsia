package lib

import "strings"

type LicenseFileTree struct {
	name     string
	children map[string]*LicenseFileTree
	files    []string
}

func (license_file_tree *LicenseFileTree) Init() {
	license_file_tree.children = make(map[string]*LicenseFileTree)
}

func (license_file_tree *LicenseFileTree) upsert(path string, files []string) {
	pieces := strings.Split(path, "/")
	curr := license_file_tree
	for _, piece := range pieces {
		if _, found := curr.children[piece]; !found {
			curr.children[piece] = &LicenseFileTree{name: piece}
			curr.children[piece].Init()
		}
		curr = curr.children[piece]
	}
	curr.files = files
}

func NewLicenses(root string) *Licenses {
	licenses := Licenses{}
	licenses.Init(root)
	return &licenses
}

func (license_file_tree *LicenseFileTree) getProjectLicense(path string) *LicenseFileTree {
	curr := license_file_tree
	var gold *LicenseFileTree
	pieces := strings.Split(path, "/")
	pieces = pieces[:len(pieces)-1]
	for _, piece := range pieces {
		if len(curr.files) > 0 {
			gold = curr
		}
		if _, found := curr.children[piece]; !found {
			break
		}
		curr = curr.children[piece]
	}
	return gold
}
