#!/usr/bin/env python
#  -*- coding: utf-8 -*-

from gitversion import get_version
version = get_version().lstrip('v')

import numpy

from setuptools import setup, Extension

setup(
    name='nicos-quickyaml',
    version=version,
    description='NICOS quick but restricted YAML dumper module',
    author='Georg Brandl',
    author_email='g.brandl@fz-juelich.de',
    license='GPL-2.0+',
    ext_modules=[Extension('quickyaml', ['quickyaml.c'],
                           include_dirs=[numpy.get_include()],
                           define_macros=[('NPY_NO_DEPRECATED_API', 'NPY_1_7_API_VERSION')],
                           )],
    install_requires=['numpy'],
)
