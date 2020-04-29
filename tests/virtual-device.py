#!/usr/bin/env python3

import sys
try:
    import gi
    import os

    from gi.repository import GLib, Gio

    import unittest
    import socket
    import struct
    import subprocess
    import shutil
    import glob
    import tempfile
except Exception as e:
    print("Missing dependencies: %s" % str(e))
    sys.exit(77)

FPrint = None

# Re-run the test with the passed wrapper if set
wrapper = os.getenv('LIBFPRINT_TEST_WRAPPER')
if wrapper:
    wrap_cmd = wrapper.split(' ') + [sys.executable, os.path.abspath(__file__)] + \
        sys.argv[1:]
    os.unsetenv('LIBFPRINT_TEST_WRAPPER')
    sys.exit(subprocess.check_call(wrap_cmd))

ctx = GLib.main_context_default()


class Connection:

    def __init__(self, addr):
        self.addr = addr

    def __enter__(self):
        self.con = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.con.connect(self.addr)
        return self.con

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.con.close()
        del self.con

class VirtualDevice(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        unittest.TestCase.setUpClass()
        cls.tmpdir = tempfile.mkdtemp(prefix='libfprint-')

        cls.sockaddr = os.path.join(cls.tmpdir, 'virtual-device.socket')
        os.environ['FP_VIRTUAL_DEVICE'] = cls.sockaddr

        cls.ctx = FPrint.Context()

        cls.dev = None
        for dev in cls.ctx.get_devices():
            # We might have a USB device in the test system that needs skipping
            if dev.get_driver() == 'virtual_device':
                cls.dev = dev
                break

        assert cls.dev is not None, "You need to compile with virtual_device for testing"

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmpdir)
        del cls.dev
        del cls.ctx
        unittest.TestCase.tearDownClass()

    def setUp(self):
        super().setUp()
        self.assertFalse(self.dev.is_open())
        self.dev.open_sync()
        self.assertTrue(self.dev.is_open())

    def tearDown(self):
        self.assertTrue(self.dev.is_open())
        self.dev.close_sync()
        self.assertFalse(self.dev.is_open())
        super().tearDown()

    def send_command(self, command, *args):
        self.assertIn(command, ['ADD'])

        with Connection(self.sockaddr) as con:
            params = ' '.join(str(p) for p in args)
            con.sendall('{} {}'.format(command, params).encode('utf-8'))

        while ctx.pending():
            ctx.iteration(False)

    def enroll_print(self, finger, match, username='testuser'):
        self._enrolled = None

        def done_cb(dev, res):
            print("Enroll done")
            self._enrolled = dev.enroll_finish(res)

        self.send_command('ADD', finger.value_nick, username, 1 if match else 0)

        template = FPrint.Print.new(self.dev)
        template.set_finger(finger)
        template.set_username(username)

        self.dev.enroll(template, None, None, tuple(), done_cb)
        while self._enrolled is None:
            ctx.iteration(False)

        return self._enrolled

    def check_verify(self, print, match, error=None):
        self._verify_match = None
        self._verify_fp = None
        self._verify_error = None

        def verify_cb(dev, res):
            try:
                self._verify_match, self._verify_fp = dev.verify_finish(res)
            except gi.repository.GLib.Error as e:
                self._verify_error = e

        self.dev.verify(print, callback=verify_cb)
        while self._verify_match is None:
            ctx.iteration(True)
        self.assertEqual(self._verify_match, match)

        if match:
            self.assertEqual(self._verify_fp, print)
        else:
            self.assertIsNone(self._verify_fp)

        if error:
            self.assertEqual(error, self._verify_error)

    def test_device_properties(self):
        self.assertEqual(self.dev.get_driver(), 'virtual_device')
        self.assertEqual(self.dev.get_device_id(), '0')
        self.assertEqual(self.dev.get_name(), 'Virtual device for debugging')
        self.assertTrue(self.dev.is_open())
        self.assertEqual(self.dev.get_scan_type(), FPrint.ScanType.SWIPE)
        self.assertEqual(self.dev.get_nr_enroll_stages(), 5)
        self.assertFalse(self.dev.supports_identify())
        self.assertFalse(self.dev.supports_capture())
        self.assertFalse(self.dev.has_storage())

    def test_enroll(self):
        matching = self.enroll_print(FPrint.Finger.LEFT_LITTLE, match=True)
        self.assertEqual(matching.get_username(), 'testuser')
        self.assertEqual(matching.get_finger(), FPrint.Finger.LEFT_LITTLE)

        self.check_verify(matching, match=True)

    def test_enroll_verify_match(self):
        matching = self.enroll_print(FPrint.Finger.RIGHT_THUMB, match=True)
        self.check_verify(matching, match=True)

    def test_enroll_verify_no_match(self):
        matching = self.enroll_print(FPrint.Finger.LEFT_RING, match=False)
        self.check_verify(matching, match=False)


if __name__ == '__main__':
    try:
        gi.require_version('FPrint', '2.0')
        from gi.repository import FPrint
    except Exception as e:
        print("Missing dependencies: %s" % str(e))
        sys.exit(77)

    # avoid writing to stderr
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
