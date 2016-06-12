nicos-quickyaml
===============

This is a fast dumper for a subset of YAML that NICOS uses for writing data files.


Prerequisites
-------------

The module requires Python (2.7 or 3.4+) and numpy (1.6+).
The test suite requires nose for running, PyYAML for verification.


Usage
-----

Install with ``setup.py`` as usual.

Basic usage::

    import quickyaml
    quickyaml.Dumper(**kwargs).dump(object, stream)

To dump lists as flowing sequences, use the ``quickyaml.flowlist`` type, which
is a subclass of ``list``.

Dumper keyword arguments:

* ``indent`` - indent width, default 4
* ``width`` - fill width for flow sequences
* ``callback`` - a callable that is called for objects of unsupported types:
  it takes the object as an argument and must return a bytes object or raise
  an exception


Tests
-----

Run tests with ``nosetests`` after ``setup.py build``.
