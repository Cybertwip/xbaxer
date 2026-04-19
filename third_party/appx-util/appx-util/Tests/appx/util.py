#
# Copyright (c) 2016-2017, Facebook, Inc.
# Copyright (c) 2021, Neal Gompa
# All rights reserved.
#
# This source code is licensed under the Mozilla Public License, version 2.0.
# For details, see the LICENSE file in the root directory of this source tree.
# Portions of this code was previously licensed under a BSD-style license.
# See the LICENSE-BSD file in the root directory of this source tree for details.

import contextlib
import os
import shutil
import subprocess
import tempfile

@contextlib.contextmanager
def temp_dir():
    dir = tempfile.mkdtemp()
    try:
        yield dir
    finally:
        shutil.rmtree(dir)

def appx_exe():
    path = os.getenv('APPX_EXE_PATH')
    if path is None:
        raise Exception('APPX_EXE_PATH environment variable must be specified')
    return path

def test_dir_path():
    return os.path.dirname(os.path.dirname(os.path.realpath(__file__)))

def test_key_path():
    return os.path.join(test_dir_path(), 'App_TemporaryKey.pfx')

def openssl_exe():
    candidates = [
        os.getenv('OPENSSL'),
        shutil.which('openssl'),
        '/opt/homebrew/opt/openssl@3/bin/openssl',
        '/usr/local/opt/openssl@3/bin/openssl',
    ]
    for candidate in candidates:
        if candidate and os.path.isfile(candidate):
            return candidate
    return None

def password_protected_test_key(output_dir, password):
    openssl = openssl_exe()
    if openssl is None:
        raise RuntimeError('openssl executable was not found')

    pem_path = os.path.join(output_dir, 'App_TemporaryKey.pem')
    passworded_pfx_path = os.path.join(output_dir, 'App_TemporaryKey.password.pfx')
    subprocess.check_call([
        openssl,
        'pkcs12',
        '-in', test_key_path(),
        '-passin', 'pass:',
        '-nodes',
        '-out', pem_path,
    ])
    subprocess.check_call([
        openssl,
        'pkcs12',
        '-export',
        '-in', pem_path,
        '-out', passworded_pfx_path,
        '-passout', f'pass:{password}',
    ])
    return passworded_pfx_path
