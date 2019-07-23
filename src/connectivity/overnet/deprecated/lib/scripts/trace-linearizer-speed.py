#!/usr/bin/python

import re
import sys

for line in open(sys.argv[1]):
  m = re.match(r'^D@([0-9.]+m?)s .* linearizer.*OFFSET:([0-9]+) .*' + sys.argv[2], line)
  if m:
    t_str = m.group(1)
    t = float(t_str[:-1])/1000 if t_str[-1]=='m' else float(t_str)
    b = int(m.group(2))
    print t, b, b*8.0/1000/1000 / t

