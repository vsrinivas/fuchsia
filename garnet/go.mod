module go.fuchsia.dev/fuchsia/garnet

go 1.15

require (
	github.com/dustin/go-humanize v1.0.0
	github.com/golang/glog v0.0.0-20160126235308-23def4e6c14b
	github.com/google/subcommands v1.2.0
	github.com/pkg/sftp v1.12.0
	go.fuchsia.dev/fuchsia/src v0.0.0-20200821151753-3226fa91b98e
	golang.org/x/crypto v0.0.0-20200820211705-5c72a883971a
)

replace (
	github.com/google/go-cmp => ../third_party/golibs/github.com/google/go-cmp
	github.com/google/subcommands => ../third_party/golibs/github.com/google/subcommands
	github.com/pkg/sftp => ../third_party/golibs/github.com/pkg/sftp
	go.fuchsia.dev/fuchsia/src => ../src
	golang.org/x/crypto => ../third_party/golibs/golang.org/x/crypto
	golang.org/x/sys => ../third_party/golibs/golang.org/x/sys
	golang.org/x/time => ../third_party/golibs/golang.org/x/time
)
