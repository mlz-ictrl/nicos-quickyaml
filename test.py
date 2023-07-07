# *****************************************************************************
# YAML dumper for NICOS, the Networked Instrument Control System of the FRM-II
# Copyright (c) 2016-2023 by the NICOS contributors (see AUTHORS)
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc.,
# 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# Module authors:
#   Georg Brandl <g.brandl@fz-juelich.de>
#
# *****************************************************************************

"""Test suite for the quickyaml dumper."""

import io
import os
import sys
import glob
import random
import collections

import numpy
import yaml
import pytest
from pytest import raises

# Support running from checkout after setup.py build.
sys.path[0:0] = glob.glob('build/lib.*-%s*' % sys.implementation.cache_tag)

import quickyaml

FUZZ_TESTS = int(os.environ.get('FUZZ_TESTS', '1000'))


def dumps(obj, **kwds):
    fp = io.BytesIO()
    kwds.setdefault('array_handling', quickyaml.ARRAY_AS_SEQ)
    quickyaml.Dumper(**kwds).dump(obj, fp)
    return fp.getvalue()


def test_primitive():
    assert dumps(None) == b'null\n'
    assert dumps(True) == b'true\n'
    assert dumps(False) == b'false\n'
    assert dumps(127) == b'127\n'
    assert dumps(2.5) == b'2.5\n'
    assert dumps(float('inf')) == b'.inf\n'
    assert dumps(float('-inf')) == b'-.inf\n'
    assert dumps(float('nan')) == b'.nan\n'


def test_strings():
    assert dumps("abc") == b'abc\n'
    assert dumps("+abc") == b'"+abc"\n'
    assert dumps("\\") == b'"\\\\"\n'
    assert dumps(" space") == b'" space"\n'
    assert dumps("space  ") == b'"space  "\n'
    assert dumps("abc\ndef") == b'"abc\\ndef"\n'
    assert dumps(u"\x85") == b'"\\x85"\n'
    assert dumps(u"Käsefuß") == 'Käsefuß\n'.encode()
    assert dumps(u"abc\u1234") == 'abc\u1234\n'.encode()
    for special in ['no', 'on', 'yes', 'off',
                    'true', 'false', 'null']:
        assert dumps(special) == f'"{special}"\n'.encode()


def test_structure():
    assert dumps([1, 2, 3], indent=2) == b'- 1\n- 2\n- 3\n'
    assert dumps((1, 2, 3), indent=2) == b'- 1\n- 2\n- 3\n'
    assert dumps({1: 2, 3: 4}) in (b'1: 2\n3: 4\n', b'3: 4\n1: 2\n')


def test_odict():
    odict = collections.OrderedDict([('a', 1), ('x', 2), (5, [1, 2])])
    assert dumps(odict) == b'a: 1\nx: 2\n5:\n-   1\n-   2\n'


def test_flowlist():
    flist = quickyaml.flowlist([1, 2.0, '+1', None, True])
    assert dumps({'a': flist}) == b'a: [1, 2.0, "+1", null, true]\n'
    flist = quickyaml.flowlist([0] * 8)
    assert dumps({'a': flist}, width=10) == \
        b'a: [0, 0, 0,\n    0, 0, 0,\n    0, 0]\n'


def test_numpy_as_seq():
    arr = numpy.array([[[1, 2], [3, 4]], [[5, 6], [7, 8]]])
    assert dumps(arr, indent=2) == \
        b'- - [1, 2]\n  - [3, 4]\n- - [5, 6]\n  - [7, 8]\n'
    arr = numpy.array([[0.0, float('nan')], [float('inf'), float('-inf')]])
    assert dumps({'counts': arr}, indent=2) == \
        b'counts:\n  - [0.0, .nan]\n  - [.inf, -.inf]\n'
    assert raises(ValueError, dumps, numpy.array(["a"]))
    arr = numpy.zeros((4, 4, 4, 4, 4)).astype(int)
    assert yaml.safe_load(dumps(arr)) == arr.tolist()
    obj = {'c': numpy.zeros((4, 4, 4, 4, 4)).astype(int)}
    assert yaml.safe_load(dumps(obj)) == {'c': obj['c'].tolist()}
    empty_dim = numpy.array([[], []])
    assert yaml.safe_load(dumps(empty_dim)) == [[], []]


def test_numpy_as_map():
    kw = {'array_handling': quickyaml.ARRAY_AS_MAP}
    arr = numpy.array([[[1, 2], [3, 4]], [[5, 6], [7, 8]]])
    assert dumps(arr, indent=2, **kw) == \
        b'0:\n  0: [1, 2]\n  1: [3, 4]\n1:\n  0: [5, 6]\n  1: [7, 8]\n'
    arr = numpy.array([[0.0, float('nan')], [float('inf'), float('-inf')]])
    assert dumps({'counts': arr}, indent=2, **kw) == \
        b'counts:\n  0: [0.0, .nan]\n  1: [.inf, -.inf]\n'
    assert raises(ValueError, dumps, numpy.array(["a"]))
    arr = numpy.zeros((4, 4, 4, 4, 4)).astype(int)
    res = [0, 0, 0, 0]
    for dim in range(4):
        res = dict((i, res) for i in range(4))
    assert yaml.safe_load(dumps(arr, **kw)) == res
    empty_dim = numpy.array([[], []])
    assert yaml.safe_load(dumps(empty_dim, **kw)) == {0: [], 1: []}


def test_callback():
    def cb(tup):
        if isinstance(tup, set):
            return b'(...)'
        raise TypeError("uh oh")

    assert dumps({'a': set((1, 2, 3))}, callback=cb) == b'a: (...)\n'
    # callback raised
    assert raises(TypeError, dumps, {'a': Ellipsis}, callback=cb)
    # no callback, unsupported type
    assert raises(ValueError, dumps, {'a': Ellipsis})


def generate_structure(depth):
    if depth == 0:
        return random.randint(0, 10)
    dis = random.random()
    n = random.randint(0, 5)
    if dis < 0.2:
        return [generate_structure(depth - 1) for _ in range(n)]
    elif dis < 0.4:
        return quickyaml.flowlist(generate_structure(0) for _ in range(n))
    elif dis < 0.6:
        return dict((i, generate_structure(depth - 1)) for i in range(n))
    elif dis < 0.7:
        return n
    elif dis < 0.75:
        return float(n)
    elif dis < 0.78:
        return float('inf')
    elif dis < 0.79:
        return float('-inf')
    elif dis < 0.8:
        return None
    elif dis < 0.82:
        return True if n % 2 == 0 else False
    elif dis < 0.9:
        return ''.join(chr(random.randint(0, 0xff))
                       for x in range(random.randint(0, 100)))
    elif dis < 0.95:
        return ''.join(chr(random.randint(0, 0xd7ff))
                       for x in range(random.randint(0, 100)))
    else:
        return random.choice(['no', 'on', 'yes', 'off',
                              'true', 'false', 'null'])


testcases = [generate_structure(20) for i in range(FUZZ_TESTS)]


@pytest.mark.parametrize('structure', testcases)
def test_fuzz_structure(structure):
    roundtripped = yaml.safe_load(dumps(structure).decode())
    assert roundtripped == structure, \
        '\nGenerated: %r\nRoundtripped: %r' % (structure, roundtripped)
