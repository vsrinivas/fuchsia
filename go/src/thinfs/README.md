# THinfs is Not a File System #

thinfs is a lightweight disk management layer. It more closely resembles a NoSQL
database than a traditional file system.

## Building ##

* Install [glide](https://glide.sh).  This will manage third-party dependencies
  in a special vendor/ directory.
* Move to the `src/thinfs` directory and run `glide install`. This will install
  dependencies into the `src/thinfs/vendor` directory.  See
  [this short explainer](http://engineeredweb.com/blog/2015/go-1.5-vendor-handling/)
  for more details about vendoring in Go.
* Build thinfs packages with `GOPATH=<path_to_thinfs> go build <package_name>`.
  See the [Go wiki](https://github.com/golang/go/wiki/GOPATH) for more details
  on what the GOPATH is used for.
