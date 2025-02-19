# Copyright 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
A tokenizer for traffic annotation definitions.
"""

from collections import namedtuple

import re

# Regexen that match a token inside the annotation definition arguments. Stored
# as a list instead of a dict, to preserve order.
#
# Order matters because otherwise, 'symbol' could be parsed before
# 'string_literal' (i.e., R"(...)" would be misinterpreted as the symbol 'R',
# followed by a string with parentheses in it).
TOKEN_REGEXEN = [
    # Comma for separating args.
    ('comma', re.compile(r'(,)')),
    # String literal. "string" or R"(string)".
    ('string_literal',
     re.compile(r'"((?:[^"]|\\.)*?)"|R"\((.*?)\)"', re.DOTALL)),
    # C++ identifier.
    ('symbol', re.compile(r'([a-zA-Z_][a-zA-Z_0-9]*)')),
    # Left parenthesis.
    ('left_paren', re.compile(r'(\()')),
    # Right parenthesis.
    ('right_paren', re.compile(r'(\))')),
]

# Number of characters to include in the context (for error reporting).
CONTEXT_LENGTH = 20

Token = namedtuple('Token', ['type', 'value', 'pos'])


class CppParsingError(Exception):
  """An error during C++ parsing/tokenizing."""

  def __init__(self, expected_type, body, pos, file_path, line_number):
    context = body[pos:pos + CONTEXT_LENGTH]
    msg = ("Expected {} in annotation definition at {}:{}.\n" +
           "near '{}'").format(expected_type, file_path, line_number, context)
    Exception.__init__(self, msg)


class Tokenizer:
  """Simple tokenizer with basic error reporting.

  Use advance() or maybe_advance() to take tokens from the string, one at a
  time.
  """

  def __init__(self, body, file_path, line_number):
    self.body = body
    self.pos = 0
    self.file_path = file_path
    self.line_number = line_number

  def _assert_token_type(self, token, expected_type):
    """Like assert(), but reports errors in a _somewhat_ useful way."""
    if token and token.type == expected_type:
      return
    # Skip whitespace to make the error message more useful.
    pos = self._skip_whitespace()
    raise CppParsingError(expected_type, self.body, pos, self.file_path,
                          self.line_number)

  def _skip_whitespace(self):
    """Return the position of the first non-whitespace character from here."""
    whitespace_re = re.compile(r'\s*')
    return whitespace_re.match(self.body, self.pos).end()

  def _get_token(self):
    """Return the token here, or None on failure."""
    # Skip initial whitespace.
    pos = self._skip_whitespace()

    # Find the token here, if there's one.
    token = None

    for (token_type, regex) in TOKEN_REGEXEN:
      re_match = regex.match(self.body, pos)
      if re_match:
        token_content = filter(lambda x: x is not None, re_match.groups())[0]
        token = Token(token_type, token_content, re_match.end())
        break

    return token

  def maybe_advance(self, expected_type):
    """Advance the tokenizer by one token if it has |expected_type|.

    Args:
      expected_type: expected |type| attribute of the token.

    Returns:
      The |value| attribute of the token if it has the right type, or None if it
      has another type.
    """
    token = self._get_token()
    if token and token.type == expected_type:
      self.pos = token.pos
      return token.value
    return None

  def advance(self, expected_type):
    """Advance the tokenizer by one token, asserting its type.

    Throws an error if the token at point has the wrong type.

    Args:
      expected_type: expected |type| attribute of the token.

    Returns:
      The |value| attribute of the token at point.
    """
    token = self._get_token()
    self._assert_token_type(token, expected_type)
    self.pos = token.pos
    return token.value
