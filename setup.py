#!/usr/bin/env python
#  -*- coding: utf-8 -*-

from setuptools import setup, Extension

setup(
    name='nicos-quickyaml',
    version='1.0',
    description='NICOS quick but restricted YAML dumper module',
    author='Georg Brandl',
    author_email='g.brandl@fz-juelich.de',
    ext_modules=[Extension('quickyaml', ['quickyaml.c'])],
)
