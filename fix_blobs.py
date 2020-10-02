# Lint as: python3

import os
import sys

os.chdir(sys.argv[1])
with os.scandir('.') as it:
  for entry in it:
    if not entry.is_file() or entry.name.startswith('.'):
      continue

    name = entry.name
    prefix = name[:2]
    name = name[2:]

    try:
      os.mkdir(prefix)
    except Exception:
      pass

    print(f'{entry.name} => {prefix}/{name}')
    os.rename(entry.name, f'{prefix}/{name}')

