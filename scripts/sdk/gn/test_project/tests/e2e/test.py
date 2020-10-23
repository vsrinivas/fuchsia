#!/usr/bin/env python2.7

import os
import sys

import unittest
import testing

# The path to GN SDK devtools. This is set in main()
TOOLS_DIR = None

class TestTesting(unittest.TestCase):
  def test_popen(self):
    """A Smoke test to verify testing.popen() works as expected"""
    with testing.popen(['echo', 'hello']) as p:
      stdout, stderr = p.communicate()
    self.assertEqual(stderr, '')
    self.assertEqual(stdout, 'hello\n')

class TestFemuSh(unittest.TestCase):
  def test_basic(self):
    femu = os.path.join(TOOLS_DIR, "femu.sh")
    args = [femu, "--headless", "--software-gpu"]

    # A message that tells us we've booted into Zircon.
    welcome_message = 'welcome to Zircon'

    # The number of output lines to search for `welcome_message`.
    # This fails if the number of lines before the message grows too large,
    # but is not flaky in the way that a timeout can be.
    max_line_count = 3000
    line_count = 0

    with testing.popen(args) as p:
      while line_count <= max_line_count:
         line_count += 1
         line = p.stdout.readline()
         if not line:
           break

         # Log the output for debugging.
         print(line)
         if welcome_message in line:
           return

    self.fail((
      'Did not find message "{}" after searching {} lines. '
      'check the output above for an error in the command that was executed.'
    ).format(welcome_message, line_count))

if __name__ == '__main__':
  TOOLS_DIR = sys.argv.pop()
  unittest.main()
