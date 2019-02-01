#!/bin/bash

format_json() {
  python << END
import json
with open('$1', 'r') as f:
  s = json.dumps(json.loads(f.read()), indent=1)
s = '\n'.join([line.rstrip() for line in s.split('\n')])
with open('$1', 'w') as f:
  f.write(s)
END
}

set -ex

fx pm snapshot \
  -manifest source/packages.manifest \
  -output source/packages.snapshot

fx pm snapshot \
  -manifest target/packages.manifest \
  -output target/packages.snapshot

# Human readable output (`fx pm delta -help` for more formatting options)
fx pm delta source/packages.snapshot target/packages.snapshot >delta.output

# JSON output
fx pm delta -output delta.json source/packages.snapshot target/packages.snapshot
format_json delta.json
