# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import time

from typing import List


class Clock:
  """A clock source, allowing faking of time in tests."""

  def wait(self, seconds: float) -> None:
    time.sleep(seconds)


class FakeClock(Clock):
  """A fake Clock implementation, for testing."""

  def __init__(self) -> None:
    self.pauses : List[float] = []

  def wait(self, seconds: float) -> None:
    self.pauses.append(seconds)


class ExponentialBackoff:
  """Track a polling period, exponentially backing off each poll."""
  def __init__(
          self,
          clock: Clock,
          min_poll_seconds: float = 1.,
          max_poll_seconds: float = 600.,
          backoff: float = 1.4142135,
    ):
    self.min_poll_seconds: float = min_poll_seconds
    self.max_poll_seconds: float = max_poll_seconds
    self.current: float = min_poll_seconds
    self.backoff: float = backoff
    self.clock: Clock = clock

  def wait(self) -> None:
    self.clock.wait(self.current)
    self.current = min(self.current * self.backoff, self.max_poll_seconds)

