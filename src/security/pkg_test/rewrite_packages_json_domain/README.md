# Build tool: packages.json Domain Name Rewriter

This build tool rewrites the domain name in URLs that appear in `packages.json`,
a manifest stored in the update package. The tool allows for creating an update
package with a custom domain name, which is necessary when using an unmodified
`pkg-resolver` repository configuration that contains a custom domain name.
