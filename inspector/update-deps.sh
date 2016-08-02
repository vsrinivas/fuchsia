#!/bin/sh

rm -rf bower_components
bower install
vulcanize --inline-scripts --inline-css --strip-comments elements/dependencies.html > elements/dependencies.vulcanized.html
rm -rf bower_components
