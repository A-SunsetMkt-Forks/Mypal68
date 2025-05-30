# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals

import codecs
import errno
import itertools
import logging
import os
import sys
import textwrap


base_dir = os.path.abspath(os.path.dirname(__file__))
sys.path.insert(0, os.path.join(base_dir, 'python', 'mozbuild'))
from mozbuild.configure import (
    ConfigureSandbox,
    TRACE,
)
from mozbuild.pythonutil import iter_modules_in_path
from mozbuild.backend.configenvironment import PartialConfigEnvironment
from mozbuild.util import (
    indented_repr,
    encode,
)
import mozpack.path as mozpath


def main(argv):
    config = {}

    sandbox = ConfigureSandbox(config, os.environ, argv)

    if os.environ.get('MOZ_CONFIGURE_TRACE'):
        sandbox._logger.setLevel(TRACE)

    sandbox.run(os.path.join(os.path.dirname(__file__), 'moz.configure'))

    if sandbox._help:
        return 0

    logging.getLogger('moz.configure').info('Creating config.status')

    old_js_configure_substs = config.pop('OLD_JS_CONFIGURE_SUBSTS', None)
    old_js_configure_defines = config.pop('OLD_JS_CONFIGURE_DEFINES', None)
    if old_js_configure_substs or old_js_configure_defines:
        js_config = config.copy()
        pwd = os.getcwd()
        try:
            try:
                os.makedirs('js/src')
            except OSError as e:
                if e.errno != errno.EEXIST:
                    raise

            os.chdir('js/src')
            js_config['OLD_CONFIGURE_SUBSTS'] = old_js_configure_substs
            js_config['OLD_CONFIGURE_DEFINES'] = old_js_configure_defines
            # The build system frontend expects $objdir/js/src/config.status
            # to have $objdir/js/src as topobjdir.
            # We want forward slashes on all platforms.
            js_config['TOPOBJDIR'] += '/js/src'
            config_status(js_config, execute=False)
        finally:
            os.chdir(pwd)

    return config_status(config)


def config_status(config, execute=True):
    # Sanitize config data to feed config.status
    # Ideally, all the backend and frontend code would handle the booleans, but
    # there are so many things involved, that it's easier to keep config.status
    # untouched for now.
    def sanitized_bools(v):
        if v is True:
            return '1'
        if v is False:
            return ''
        return v

    sanitized_config = {}
    sanitized_config['substs'] = {
        k: sanitized_bools(v) for k, v in config.iteritems()
        if k not in ('DEFINES', 'TOPSRCDIR', 'TOPOBJDIR', 'CONFIG_STATUS_DEPS',
                     'OLD_CONFIGURE_SUBSTS', 'OLD_CONFIGURE_DEFINES')
    }
    for k, v in config['OLD_CONFIGURE_SUBSTS']:
        sanitized_config['substs'][k] = sanitized_bools(v)
    sanitized_config['defines'] = {
        k: sanitized_bools(v) for k, v in config['DEFINES'].iteritems()
    }
    for k, v in config['OLD_CONFIGURE_DEFINES']:
        sanitized_config['defines'][k] = sanitized_bools(v)
    sanitized_config['topsrcdir'] = config['TOPSRCDIR']
    sanitized_config['topobjdir'] = config['TOPOBJDIR']
    sanitized_config['mozconfig'] = config.get('MOZCONFIG')

    # Create config.status. Eventually, we'll want to just do the work it does
    # here, when we're able to skip configure tests/use cached results/not rely
    # on autoconf.
    encoding = 'mbcs' if sys.platform == 'win32' else 'utf-8'
    with codecs.open('config.status', 'w', encoding) as fh:
        fh.write(textwrap.dedent('''\
            #!%(python)s
            # coding=%(encoding)s
            from __future__ import unicode_literals
            from mozbuild.util import encode
            encoding = '%(encoding)s'
        ''') % {'python': config['PYTHON'], 'encoding': encoding})
        # A lot of the build backend code is currently expecting byte
        # strings and breaks in subtle ways with unicode strings. (bug 1296508)
        for k, v in sanitized_config.iteritems():
            fh.write('%s = encode(%s, encoding)\n' % (k, indented_repr(v)))
        fh.write("__all__ = ['topobjdir', 'topsrcdir', 'defines', "
                 "'substs', 'mozconfig']")

        if execute:
            fh.write(textwrap.dedent('''
                if __name__ == '__main__':
                    from mozbuild.util import patch_main
                    patch_main()
                    from mozbuild.config_status import config_status
                    args = dict([(name, globals()[name]) for name in __all__])
                    config_status(**args)
            '''))

    partial_config = PartialConfigEnvironment(config['TOPOBJDIR'])
    partial_config.write_vars(sanitized_config)

    # Write out a file so the build backend knows to re-run configure when
    # relevant Python changes.
    with open('config_status_deps.in', 'w') as fh:
        for f in itertools.chain(config['CONFIG_STATUS_DEPS'],
                                 iter_modules_in_path(config['TOPOBJDIR'],
                                                      config['TOPSRCDIR'])):
            fh.write('%s\n' % mozpath.normpath(f))

    # Other things than us are going to run this file, so we need to give it
    # executable permissions.
    os.chmod('config.status', 0o755)
    if execute:
        from mozbuild.config_status import config_status

        # Some values in sanitized_config also have more complex types, such as
        # EnumString, which using when calling config_status would currently
        # break the build, as well as making it inconsistent with re-running
        # config.status. Fortunately, EnumString derives from unicode, so it's
        # covered by converting unicode strings.

        # A lot of the build backend code is currently expecting byte strings
        # and breaks in subtle ways with unicode strings.
        return config_status(args=[], **encode(sanitized_config, encoding))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
