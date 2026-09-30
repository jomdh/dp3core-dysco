# SPDX-License-Identifier: GPL-3.0-only
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))

from fixtures import GOLDEN_FIXTURE_ARGS, make_fixture  # noqa: E402


@pytest.fixture(scope="session")
def golden_fixture():
    return make_fixture(*GOLDEN_FIXTURE_ARGS)
