# Example HTTP Server

This example http server runs locally on a fuchsia device and serves files from
a single public directory.

## Build

```
$ fx set core.x64 --with //garnet/examples/http/example_http_server
$ fx build
```

## Usage

To run the server on the fuchsia device, from fx shell:

```
run fuchsia-pkg://fuchsia.com/example_http_server#meta/example_http_server.cmx
```

To view the served webpage from the browser:
* [Googlers only] Disable the BeyondCorp proxy in the browser by setting the proxy
   settings to "Off: Direct" in the extension.
* On your fuchsia device, run ifconfig to determine it's IP address.
* Open `http://192.168.42.42/index.html` in your browser (or whatever the IP address
  of your fuchsia device is).
* You should see "Hello world!" displayed in your browser.


To get the webpage with curl:
* `curl 192.168.42.47/index.html` (using your device's IP address).


