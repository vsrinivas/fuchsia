# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

__all__ = ['Deny', 'Identifier', 'ScopedIdentifier', 'Style', 'Use']

from dataclasses import dataclass, field
from typing import List, Callable, IO, Optional, Tuple
from functools import reduce


@dataclass
class Deny:
    styles: List[str] = field(default_factory=list)
    uses: List[str] = field(default_factory=list)
    bindings: List[str] = field(default_factory=list)

    def matches(self, style: 'Style', use: 'Use') -> bool:
        return not (
            self.styles and style.name not in self.styles or
            self.uses and use.name not in self.uses)


@dataclass
class Identifier:
    name: str
    tag: int
    deny: List[Deny] = field(default_factory=list)

    @property
    def parts(self) -> List[str]:
        return self.name.split('_')

    def scoped(self, style: 'Style', use: 'Use') -> 'ScopedIdentifier':
        # All the deny rules for this style & use
        denies = [deny for deny in self.deny if deny.matches(style, use)]
        # Bindings deny list
        bindings_denylist = [
            binding for deny in denies for binding in deny.bindings
        ]

        return ScopedIdentifier(
            style(self.parts), self.tag, style, use,
            any(d.bindings == [] for d in denies),
            ','.join(sorted(set(bindings_denylist))))


@dataclass
class ScopedIdentifier:
    name: str
    tag: int
    style: 'Style'
    use: 'Use'
    denied: bool
    bindings_denylist: str

    def __str__(self):
        return self.name

    @property
    def decl_attributes(self) -> str:
        '''Attributes to put on a declaration with this name.'''
        if self.bindings_denylist:
            return f'[BindingsDenylist="{self.bindings_denylist}"]\n'
        else:
            return ''


@dataclass
class Style:
    name: str
    func: Tuple[Callable[[List[str]], str]]

    def __call__(self, parts: List[str]) -> str:
        return self.func[0](parts)


@dataclass
class Use:
    name: str
    func: Tuple[Callable[[IO, List[ScopedIdentifier]], None]]

    def __call__(self, f: IO, idents: List[ScopedIdentifier]):
        return self.func[0](f, idents)
