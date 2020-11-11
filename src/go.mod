module go.fuchsia.dev/fuchsia/src

go 1.15

require (
	github.com/dustin/go-humanize v1.0.0
	github.com/flynn/go-tuf v0.0.0-20200724142817-bf368c57efac
	github.com/fsnotify/fsnotify v1.4.9
	github.com/google/go-cmp v0.5.3-0.20201020212313-ab46b8bd0abd
	github.com/pkg/errors v0.9.1
	github.com/pkg/sftp v1.12.0
	github.com/tent/canonical-json-go v0.0.0-20130607151641-96e4ba3a7613 // indirect
	go.fuchsia.dev/fuchsia/garnet v0.0.0-20200821151753-3226fa91b98e
	go.fuchsia.dev/fuchsia/tools v0.0.0-20200821151753-3226fa91b98e
	go.uber.org/multierr v1.6.0
	golang.org/x/crypto v0.0.0-20200820211705-5c72a883971a
	golang.org/x/net v0.0.0-20200822124328-c89045814202 // indirect
	golang.org/x/sys v0.0.0-20200828081204-131dc92a58d5 // indirect
	gvisor.dev/gvisor v0.0.0-20200828123349-7bc9f9b47f61
)

replace (
	github.com/flynn/go-tuf => ../third_party/golibs/github.com/flynn/go-tuf
	github.com/fsnotify/fsnotify => ../third_party/golibs/github.com/fsnotify/fsnotify
	github.com/google/go-cmp => ../third_party/golibs/github.com/google/go-cmp
	github.com/pkg/sftp => ../third_party/golibs/github.com/pkg/sftp
	go.fuchsia.dev/fuchsia/garnet => ../garnet
	go.fuchsia.dev/fuchsia/tools => ../tools
	go.uber.org/multierr => ../third_party/golibs/go.uber.org/multierr
	golang.org/x/crypto => ../third_party/golibs/golang.org/x/crypto
	golang.org/x/net => ../third_party/golibs/golang.org/x/net
	golang.org/x/sys => ../third_party/golibs/golang.org/x/sys
	golang.org/x/xerrors => ../third_party/golibs/golang.org/x/xerrors
	gvisor.dev/gvisor => ../third_party/golibs/gvisor.dev/gvisor
)
