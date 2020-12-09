#!/usr/bin/python3

import gi
import os
import unittest
import sys

gi.require_version('FPrint', '2.0')
from gi.repository import FPrint, GLib

ctx = GLib.main_context_default()

# Order matters!
unittest.TestLoader.sortTestMethodsUsing = None

class StorageDevice(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ctx = FPrint.Context()
        cls.ctx.enumerate()

        cls.dev = None
        assert len(cls.ctx.get_devices()) == 1
        cls.dev = cls.ctx.get_devices()[0]
        assert cls.dev.get_driver() == os.getenv('FP_TEST_DRIVER_NAME')
        cls.dev.open_sync()

    @classmethod
    def tearDownClass(cls):
        cls.dev.close_sync()
        del cls.dev
        del cls.ctx

    def setUp(self):
        self.cls = __class__

    def get_print_template(self, finger=FPrint.Finger.UNKNOWN):
        template = FPrint.Print.new(self.dev)
        template.set_finger(finger)
        template.set_description('Enroll test template')
        return template

    def clone_prints(self, prints):
        cloned = []
        for p in prints:
            cloned.insert(p.props.finger, FPrint.Print.deserialize(p.serialize()))
            self.assertTrue(cloned[p.props.finger].equal(p))

        return cloned

    def save_local_prints(self, prints):
        self.cls.local_prints = self.clone_prints(prints)

    def get_new_stored_print(self, finger=FPrint.Finger.UNKNOWN):
        return FPrint.Print.deserialize(self.cls.local_prints[finger].serialize())

    def test_enroll(self):
        def enroll_progress(*args):
            print('enroll progress: ' + str(args))

        def enroll_done(dev, res):
            nonlocal enrolled_print
            enrolled_print = dev.enroll_finish(res)
            print("enroll done", enrolled_print)
            self.assertIsNotNone(enrolled_print)
            self.assertEqual(self.dev.get_finger_status(), FPrint.FingerStatusFlags.NONE)

        print("enrolling")
        enrolled_print = None
        template = self.get_print_template()
        self.dev.enroll(template, None, enroll_progress, callback=enroll_done)
        del template
        while not enrolled_print:
            ctx.iteration(True)
        self.cls.enrolled_print = enrolled_print

    def test_list(self):
        print("listing")
        stored = self.dev.list_prints_sync()
        print("listing done")
        self.assertEqual(len(stored), 1)
        self.assertTrue(stored[0].equal(self.cls.enrolled_print))
        del self.cls.enrolled_print

        # We use prints deserialized from the storage here to replicate what
        # fprintd could do, so checking that templates are valild.
        self.save_local_prints(stored)
        del stored

    def test_verify_identify_delete(self):
        def verify_cb(dev, res):
            nonlocal verify_match, verify_print
            try:
                verify_match, verify_print = dev.verify_finish(res)
                retry = not verify_match
            except GLib.Error as e:
                verify_match = False
                if e.domain != FPrint.device_retry_quark():
                    raise(e)

            if not verify_match:
                print('retrying verification')
                self.dev.verify(self.get_new_stored_print(), callback=verify_cb)
                return

            self.assertEqual(self.dev.get_finger_status(), FPrint.FingerStatusFlags.NONE)
            self.assertTrue(verify_match)
            print("verify done", verify_match, verify_print)

        print("verifying")
        verify_match = False
        verify_print = None
        p = self.get_new_stored_print()
        self.dev.verify(p, callback=verify_cb)
        del p
        while not verify_match:
            ctx.iteration(True)

        def identify_cb(dev, res):
            nonlocal identified
            try:
                identify_match, identify_print = dev.identify_finish(res)
                identified = identify_match != None
            except GLib.Error as e:
                identified = False
                print('Error',e,e.domain,'quark',FPrint.device_retry_quark())
                if int(e.domain) != FPrint.device_retry_quark():
                    raise(e)

            if not identified:
                print('retrying identification')
                self.dev.identify(self.clone_prints(self.local_prints), callback=identify_cb)
                return

            print('indentification done: ', identify_match, identify_print)
            self.assertTrue(identify_match.equal(identify_print))

        print('identifying')
        identified = False
        gallery = self.clone_prints(self.local_prints)
        self.dev.identify(gallery, callback=identify_cb)
        del gallery

        while not identified:
            ctx.iteration(True)

        print("deleting")
        self.dev.delete_print_sync(self.get_new_stored_print())
        print("delete done")

if __name__ == '__main__':
    # avoid writing to stderr
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
