# Copyright 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Package grit.tool.update_resource_ids

Updates GRID resource_ids from linked GRD files, while preserving structure.

A resource_ids file is a JSON dict (with Python comments) that maps GRD paths
to *items*. Item order is ignored by GRIT, but is important since it establishes
a narrative of item dependency (needs non-overlapping IDs) and mutual exclusion
(allows ID overlap). Example:

{
  # The first entry in the file, SRCDIR, is special: It is a relative path from
  # this file to the base of your checkout.
  "SRCDIR": "../..",

  # First GRD file. This entry is an "Item".
  "1.grd": {
    "messages": [400],  # "Tag".
  },
  # Depends on 1.grd, i.e., 500 >= 400 + (# of IDs used in 1.grd).
  "2a.grd": {
    "includes": [500],    # "Tag".
    "structures": [510],  # "Tag" (etc.).
  },
  # Depends on 2a.grd.
  "3a.grd": {
    "includes": [1000],
  },
  # Depends on 2a.grd, but overlaps with 3b.grd due to mutually exclusivity.
  "3b.grd": {
    "includes": [1000],
  },
  # Depends on {3a.grd, 3b.grd}.
  "4.grd": {
    "META": {"join": 2},  # Hint for update_resource_ids.
    "structures": [1500],
  },
  # Depends on 1.grd but overlaps with 2a.grd.
  "2b.grd": {
    "includes": [500],
    "structures": [540],
  },
  # Depends on {4.grd, 2b.grd}.
  "5.grd": {
    "META": {"join": 2},  # Hint for update_resource_ids.
    "includes": [600],
  },
  # Depends on 5.grd. File is generated, so hint is needed for sizes.
  "<(SHARED_INTERMEDIATE_DIR)/6.grd": {
    "META": {"sizes": {"includes": [10]}},
    "includes": [700],
  },
}

The "structure" within a resouces_ids file are as follows:
1. Comments and spacing.
2. Item ordering, to establish dependency and grouping.
3. Special provision to allow ID overlaps from mutual exclusion.

This module parses a resource_ids file, reads ID usages from GRD files it refers
to, and generates an updated version of the resource_ids file while preserving
structure elements 1-3 stated above.
"""

from __future__ import print_function

import collections
import getopt
import os
import shutil
import sys
import tempfile

from grit.tool import interface
from grit.tool.update_resource_ids import assigner, common, parser, reader


def _ReadData(input_file):
  if input_file == '-':
    data = sys.stdin.read()
    file_dir = os.getcwd()
  else:
    with open(input_file, 'rt') as f:
      data = f.read()
    file_dir = os.path.dirname(input_file)
  return data, file_dir


def _MultiReplace(data, repl):
  """Multi-replacement of text |data| by ranges and replacement text.

  Args:
    data: Original text.
    repl: List of (lo, hi, s) tuples, specifying that |data[lo:hi]| should be
      replaced with |s|. The ranges must be inside |data|, and not overlap.
  Returns: New text.
  """
  res = []
  prev = 0
  for (lo, hi, s) in sorted(repl):
    if prev < lo:
      res.append(data[prev:lo])
    res.append(s)
    prev = hi
  res.append(data[prev:])
  return ''.join(res)


def _WriteFileIfChanged(output, new_data):
  if not output:
    sys.stdout.write(new_data)
    return

  # Avoid touching outputs if file contents has not changed so that ninja
  # does not rebuild dependent when not necessary.
  if os.path.exists(output) and _ReadData(output)[0] == new_data:
    return

  # Write to a temporary file to ensure atomic changes.
  with tempfile.NamedTemporaryFile('wt', delete=False) as f:
    f.write(new_data)
  shutil.move(f.name, output)


class _Args:
  """Encapsulated arguments for this module."""
  def __init__(self):
    self.add_header = False
    self.analyze_inputs = False
    self.count = False
    self.depfile = None
    self.fake = False
    self.input = None
    self.naive = False
    self.output = None
    self.parse = False
    self.tokenize = False

  @staticmethod
  def Parse(raw_args):
    own_opts, raw_args = getopt.getopt(raw_args, 'o:cpt', [
        'add-header',
        'analyze-inputs',
        'count',
        'depfile=',
        'fake',
        'naive',
        'parse',
        'tokenize',
    ])
    args = _Args();
    if not len(raw_args) == 1:
      print('grit update_resource_ids takes exactly one argument, the path to '
            'the resource ids file.')
      return 2
    args.input = raw_args[0]
    for (key, val) in own_opts:
      if key == '-o':
        args.output = val
      elif key == '--add-header':
        args.add_header = True
      elif key == '--analyze-inputs':
        args.analyze_inputs = True
      elif key in ('--count', '-c'):
        args.count = True
      elif key == '--depfile':
        args.depfile = val
      elif key == '--fake':
        args.fake = True
      elif key == '--naive':
        args.naive = True
      elif key in ('--parse', '-p'):
        args.parse = True
      elif key in ('--tokenize', '-t'):
        args.tokenize = True
    return args


class UpdateResourceIds(interface.Tool):
  """Updates all start IDs in an resource_ids file by reading all GRD files it
refers to, estimating the number of required IDs of each type, then rewrites
start IDs while preserving structure.

Usage: grit update_resource_ids [--parse|-p] [--read-grd|-r] [--tokenize|-t]
                                [--naive] [--fake] [-o OUTPUT_FILE]
                                [--analyze-inputs] [--depfile DEPFILE]
                                [--add-header] RESOURCE_IDS_FILE

RESOURCE_IDS_FILE is the path of the input resource_ids file.

The -o option redirects output (default stdout) to OUPTUT_FILE, which can also
be RESOURCE_IDS_FILE.

Other options:

  -E NAME=VALUE     Sets environment variable NAME to VALUE (within grit).

  --count|-c        Parses RESOURCE_IDS_FILE, reads the GRD files, and prints
                    required sizes.

  --fake            For testing: Skips reading GRD files, and assigns 10 as the
                    usage of every tag.

  --naive           Use naive coarse assigner.

  --parse|-p        Parses RESOURCE_IDS_FILE and dumps its nodes to console.

  --tokenize|-t     Tokenizes RESOURCE_IDS_FILE and reprints it as syntax-
                    highlighted output.

  --depfile=DEPFILE Write out a depfile for ninja to know about dependencies.
  --analyze-inputs  Writes dependencies to stdout.
  --add-header      Adds a "THIS FILE IS GENERATED" header to the output.
"""

  def __init(self):
    super(UpdateResourceIds, self).__init__()

  def ShortDescription(self):
    return 'Updates a resource_ids file based on usage, preserving structure'

  def _DumpTokens(self, data, tok_gen):
    # Reprint |data| with syntax highlight.
    color_map = {
        '#': common.Color.GRAY,
        'S': common.Color.CYAN,
        '0': common.Color.RED,
        '{': common.Color.YELLOW,
        '}': common.Color.YELLOW,
        '[': common.Color.GREEN,
        ']': common.Color.GREEN,
        ':': common.Color.MAGENTA,
        ',': common.Color.MAGENTA,
    }
    for t, lo, hi in tok_gen:
      c = color_map.get(t, common.Color.NONE)
      sys.stdout.write(c(data[lo:hi]))

  def _DumpRootObj(self, root_obj):
    print(root_obj)

  def _DumpResourceCounts(self, usage_gen):
    tot = collections.Counter()
    for item, tag_name_to_usage in usage_gen:
      c = common.Color.YELLOW if item.grd.startswith('<') else common.Color.CYAN
      print('%s: %r' % (c(item.grd), dict(tag_name_to_usage)))
      tot += collections.Counter(tag_name_to_usage)
    print(common.Color.GRAY('-' * 80))
    print('%s: %r' % (common.Color.GREEN('Total'), dict(tot)))
    print('%s: %d' % (common.Color.GREEN('Grand Total'), sum(tot.values())))

  def Run(self, opts, raw_args):
    self.SetOptions(opts)

    args = _Args.Parse(raw_args)
    data, file_dir = _ReadData(args.input)

    tok_gen = parser.Tokenize(data)
    if args.tokenize:
      return self._DumpTokens(data, tok_gen)

    root_obj = parser.ResourceIdParser(data, tok_gen).Parse()
    if args.parse:
      return self._DumpRootObj(root_obj)
    item_list = common.BuildItemList(root_obj)

    src_dir = os.path.abspath(os.sep.join([file_dir, root_obj['SRCDIR'].val]))
    seen_files = set()
    usage_gen = reader.GenerateResourceUsages(item_list, src_dir, args.fake,
                                              seen_files)
    if args.count:
      return self._DumpResourceCounts(usage_gen)
    for item, tag_name_to_usage in usage_gen:
      item.SetUsages(tag_name_to_usage)

    if args.analyze_inputs:
      print('\n'.join(sorted(seen_files)))
      return 0

    new_ids_gen = assigner.GenerateNewIds(item_list, args.naive)
    # Create replacement specs usable by _MultiReplace().
    repl = [(tag.lo, tag.hi, str(new_id)) for tag, new_id in new_ids_gen]
    rel_input_dir = args.input
    # Update "SRCDIR" entry if output is specified.
    if args.output:
      new_srcdir = os.path.relpath(src_dir, os.path.dirname(args.output))
      repl.append((root_obj['SRCDIR'].lo, root_obj['SRCDIR'].hi,
                   repr(new_srcdir)))
      rel_input_dir = os.path.join('$SRCDIR',
                                   os.path.relpath(rel_input_dir, new_srcdir))

    new_data = _MultiReplace(data, repl)
    if args.add_header:
      header = []
      header.append('# GENERATED FILE.')
      header.append('# Edit %s instead.' % rel_input_dir)
      header.append('#' * 80)
      new_data = '\n'.join(header + ['']) + new_data
    _WriteFileIfChanged(args.output, new_data)

    if args.depfile:
      deps_data = '{}: {}'.format(args.output, ' '.join(sorted(seen_files)))
      _WriteFileIfChanged(args.depfile, deps_data)
