# Thinfs is Not a File System #

thinfs is a lightweight disk management layer.

## Get the source  ##

The easiest way to get the source is to use `go get`:

    go get fuchsia.googlesource.com/thinfs


## Building ##

* Install [glide](https://glide.sh).  This will manage third-party dependencies
  in a special `vendor/` directory.
* Change to the thinfs directory and run `glide install`. This will install
  dependencies into the `vendor/` directory.  See
  [this short explainer](http://engineeredweb.com/blog/2015/go-1.5-vendor-handling/)
  for more details about vendoring in Go.
* Build thinfs packages with `go build <package_name>`.
