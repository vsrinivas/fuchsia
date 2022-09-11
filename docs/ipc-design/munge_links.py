#!/usr/bin/env python3

# Munge links in HTML produced by a Markdown formatter.  Change the
# links to refer to .html files instead of .md files.

import re
import sys


def munge_link(match):
    return match.group().replace('.md', '.html')


def main():
    data = sys.stdin.read()
    data = re.sub(r'<a href="[a-z-]+\.md">', munge_link, data)
    sys.stdout.write(data)


if __name__ == '__main__':
    main()
