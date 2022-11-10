# root_ssl_certificates

This directory contains the `root_ssl_certificates` package, which is used by
`appmgr` to provide the `root-ssl-certificates` sandbox feature.

The certificates file, `third_party/cert.pem`, is updated using the
`roll_certs.go` script, which pulls from Mozilla's root cert bundle at
https://hg.mozilla.org/mozilla-central/raw-file/tip/security/nss/lib/ckfw/builtins/certdata.txt
and converts it to a single PEM file using
`third_party/convert_mozilla_certdata.go`. To update, simply run `go run
roll_certs.go` and, if this results in any update to `third_party/cert.pem`,
check in the new versions of both `third_party/cert.pem` and
`third_party/cert.stamp`.

This includes the `third_party/convert_mozilla_certdata.go` tool, which is used
by `roll_certs.go` to extract the bundle of Mozilla root certificates. This tool
is originally from https://github.com/agl/extract-nss-root-certs. The version of
the tool here is taken from commit 492d8c95628eb861a9f1467099936bc2b1fd6a7b.

The contents of `third_party/cert.pem` are covered by the license file
`third_party/LICENSE.MPLv2`.
