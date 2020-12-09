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
identify_match, identify_print = d.identify_sync(stored)
assert identify_match.equal(identify_print)
print("identify done")
del identify_match
del identify_print

print("deleting")
d.delete_print_sync(stored[0])
print("delete done")
d.close_sync()

del d
del c
del stored


# Enroll and identify with deserialized data, as fprintd does

c = FPrint.Context()
c.enumerate()

d = c.get_devices()[0]
d.open_sync() == True
template = FPrint.Print.new(d)

print("enrolling")
p = d.enroll_sync(template, None, enroll_progress, None)
print("enroll done")

deserialized_prints = [FPrint.Print.deserialize(p.serialize())]
assert deserialized_prints[-1].equal(p)
del p

print('async identifying')
d.identify(deserialized_prints, callback=identify_done)
del deserialized_prints

while not identified:
    ctx.iteration(True)

del d

del c
