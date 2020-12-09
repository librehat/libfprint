#!/usr/bin/python3

import gi
gi.require_version('FPrint', '2.0')
from gi.repository import FPrint, GLib

ctx = GLib.main_context_default()

c = FPrint.Context()
c.enumerate()
devices = c.get_devices()

d = devices[0]
del devices

assert d.get_driver() == "goodixmoc"

d.open_sync()

template = FPrint.Print.new(d)

identified = False

def enroll_progress(*args):
    print('enroll progress: ' + str(args))

def identify_done(dev, res):
    global identified
    identified = True
    identify_match, identify_print = dev.identify_finish(res)
    print('indentification_done: ', identify_match, identify_print)
    assert identify_match.equal(identify_print)

# List, enroll, list, verify, identify, delete
print("enrolling")
p = d.enroll_sync(template, None, enroll_progress, None)
print("enroll done")

import os
print(os.environ)

print("listing")
stored = d.list_prints_sync()
print("listing done")
assert len(stored) == 1
assert stored[0].equal(p)
print("verifying")
verify_res, verify_print = d.verify_sync(p)
print("verify done")
del p
assert verify_res == True
print("identifying")
others = []
for p in stored:
    others.append(FPrint.Print.deserialize(p.serialize()))
    assert others[-1].equal(p)
del stored

d.identify(others, callback=identify_done)
del others

while not identified:
    ctx.iteration(True)

print("identify done")

print("deleting")
d.delete_print_sync(d.list_prints_sync()[0])
print("delete done")
d.close_sync()

del d
del c
