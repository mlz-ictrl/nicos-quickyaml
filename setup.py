#!/usr/bin/env python
#  -*- coding: utf-8 -*-

from numpy.distutils.core import setup, Extension
from gitversion import get_version

setup(
    name='nicos-quickyaml',
    version=get_version().lstrip('v'),
    description='NICOS quick but restricted YAML dumper module',
    author='Georg Brandl',
    author_email='g.brandl@fz-juelich.de',
    ext_modules=[Extension('quickyaml', ['quickyaml.c'])],
)
