# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""fd.py is a fascinating directory changer to save your time in typing.

fd.py is intended to be used through shell function "fd()" in fx-env.sh.

See examples by
$ fd --help

fd stores two helper files, fd.txt and fd.pickle in $FUCHSIA_DIR/out/.
If that directory does not exists, fd will create one.
"""

import argparse
import os
import pickle
import sys
import termios
import tty

SEARCH_BASE = os.environ['FUCHSIA_DIR']  # or 'HOME'
STORE_DIR = SEARCH_BASE + '/out/'
DIRS_FILE = STORE_DIR + 'fd.txt'
PICKLE_FILE = STORE_DIR + 'fd.pickle'

EXCLUDE_DIRS = [
    '"*/.git"', './build', './out', './prebuilt', './third_party',
    './zircon/build', './cmake-build-debug', './zircon/third_party',
]


def eprint(*args, **kwargs):
  print(*args, file=sys.stderr, **kwargs)


class Trie(object):
  """Class Trie.
  """

  def __init__(self):
    self.name = ''  # != path up to here from the root. Key is a valid
    # complete one.
    self.vals = []
    self.kids = {}

  def __getitem__(self, name, idx=0):
    if self.name == name:
      return self.vals
    if idx == name.__len__() or name[idx] not in self.kids:
      return None
    return self.kids[name[idx]].__getitem__(name, idx + 1)

  def __setitem__(self, name, val, idx=0):
    if idx < name.__len__():
      self.kids.setdefault(name[idx], Trie()).__setitem__(name, val, idx + 1)
      return
    self.name = name
    self.vals.append(val)

  def __contains__(self, name):
    return self[name] is not None

  def walk(self):
    descendants = []
    if self.name:
      descendants.append(self.name)
    for k in self.kids:
      descendants.extend(self.kids[k].walk())
    return descendants

  def prefixed(self, name, idx=0):
    if idx < name.__len__():
      if name[idx] in self.kids:
        return self.kids[name[idx]].prefixed(name, idx + 1)
      return []

    return self.walk()


def build_trie():
  """build trie.

  Returns:
    Trie
  """

  def build_find_cmd():
    paths = []
    for path in EXCLUDE_DIRS:
      paths.append('{} {}'.format('-path', path))
    return (r'cd {}; find . \( {} \) -prune -o -type d -print > '
            '{}').format(SEARCH_BASE, ' -o '.join(paths), DIRS_FILE)

  if not os.path.exists(STORE_DIR):
    os.makedirs(STORE_DIR)

  cmd_str = build_find_cmd()
  os.system(cmd_str)

  t = Trie()
  with open(DIRS_FILE, 'r') as f:
    for line in f:
      line = line[2:][:-1]
      tokens = line.split('/')
      if tokens.__len__() == 0:
        continue
      target = tokens[-1]

      t[target] = line

  return t


def get_trie():
  """get_trie.

  Returns:
    trie
  """

  def save_pickle(obj):
    with open(PICKLE_FILE, 'wb+') as f:
      pickle.dump(obj, f, protocol=pickle.HIGHEST_PROTOCOL)

  def load_pickle():
    with open(PICKLE_FILE, 'rb') as f:
      return pickle.load(f)

  if os.path.exists(PICKLE_FILE):
    return load_pickle()

  t = build_trie()
  save_pickle(t)
  return t


def button(idx):
  """button maps idx to an ascii value.
  """
  ascii = 0
  if 0 <= idx <= 8:
    ascii = ord('1') + idx
  elif 9 <= idx <= 34:
    ascii = ord('a') + idx - 9
  elif 35 <= idx <= 60:
    ascii = ord('A') + idx - 35
  elif 61 <= idx <= 75:
    ascii = ord('!') + idx - 61
  return str(chr(ascii))


def get_button():  # Unix way
  fd = sys.stdin.fileno()
  old_settings = termios.tcgetattr(fd)
  try:
    tty.setraw(sys.stdin.fileno())
    ch = sys.stdin.read(1)
  finally:
    termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
  return ch


def choose_options(t, key, choice):
  # Build options by the given key
  if key in t:
    options = t[key]
  else:
    prefixed_keys = t.prefixed(key)
    options = []
    for pk in prefixed_keys:
      options.extend(t[pk])

  options = sorted(options)
  if options.__len__() == 0:
    eprint('No such directory: {}'.format(key))
    return None
  elif options.__len__() == 1:
    return options[0]
  elif options.__len__() > 75:  # See def button() for the limit.
    eprint('Too many ({}) results for "{}". '
           'Refine your prefix or time to buy 4K '
           'monitor\n'.format(options.__len__(), key))
    return None

  def list_choices(l):
    for i in range(l.__len__()):
      eprint('[{}] {}'.format(button(i), l[i]))
    eprint()

  choice_dic = {}
  for idx, val in enumerate(options):
    choice_dic[button(idx)] = val

  if choice is not None and choice not in choice_dic:
    # Invalid pre-choice
    eprint('Choice "{}" not available\n'.format(choice))

  if choice not in choice_dic:
    list_choices(options)
    choice = get_button()
    if choice not in choice_dic:
      return None

  return choice_dic[choice]


def main():

  def parse_cmdline():
    example_commands = """

[eg] # Use "fd" for autocompletion (See //scripts/fx-env.sh)
  $ fd ral        # change directory to an only option: ralink
  $ fd wlan       # shows all "wlan" directories and ask to choose
  $ fd wlan 3     # change directory matching to option 3 of "fd wlan"
  $ fd [TAB]      # Autocomplete subdirectories from the current directory
  $ fd //[TAB]    # Autocomplete subdirectories from ${FUCHSIA_DIR}
  $ fd --rebuild  # rebuilds the directory structure cache
"""
    p = argparse.ArgumentParser(
        description='A fascinating directory changer',
        epilog=example_commands,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    p.add_argument(
        '--rebuild', action='store_true', help='rebuild the directory DB')
    p.add_argument('--base', type=str, default=None)
    p.add_argument('target', nargs='?', default='')
    p.add_argument('choice', nargs='?', default=None)

    # Redirect help messages to stderr
    if len(sys.argv) == 2:
      if sys.argv[1] in ['-h', '--help']:
        eprint(p.format_help())
        print('.')  # Stay at the current directory
        sys.exit(0)

    return p.parse_args()

  def get_abs_path(relative_dir):
    if relative_dir is not None:
      return os.path.join(SEARCH_BASE, relative_dir)
    return os.getcwd()

  def derive_dest(target):
    if not target:
      # To test if this command was invoked just to rebuild
      return get_abs_path('.') if args.rebuild is False else os.getcwd()

    if target[:2] == '//':
      target = target[2:]

    candidate = target

    # Do not guess-work when the user specifies an option to intend to use.
    # Do guess work otherwise.
    if not args.choice:
      if os.path.exists(candidate):
        return candidate

      candidate = get_abs_path(target)
      if os.path.exists(candidate):
        return candidate

      candidate = os.path.abspath(target)
      if os.path.exists(candidate):
        return candidate

    t = get_trie()
    return get_abs_path(choose_options(t, target, args.choice))

  args = parse_cmdline()
  if args.base:
    global SEARCH_BASE
    SEARCH_BASE = args.base

  if args.rebuild:
    os.remove(PICKLE_FILE)

  dest = derive_dest(args.target)
  dest = os.path.normpath(dest)
  print(dest)


if __name__ == '__main__':
  try:
    main()
  except Exception as e:  # Catch all
    eprint(e.message, e.args)
    print('.')  # Stay at the current directory upon exception
