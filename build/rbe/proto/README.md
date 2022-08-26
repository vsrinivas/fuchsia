# RBE Metrics and Logs protos

This directory is a staging area for .proto and pb2.py sources needed for
processing and uploading data to BigQuery tables, which is done when `fx
build-metrics` is enabled.

## Updating

To populate the staging area, run `build/rbe/proto/refresh.sh`.

The staging area will be git-ignored.

