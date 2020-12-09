#!/usr/bin/env python3

import sys

if len(sys.argv) < 2:
    print('A ioctl file is expected')
    sys.exit(1)

ioctl = sys.argv[1]

with open(sys.argv[1], 'r') as ioctl:
    command = None

    for line in ioctl.readlines():
        line = line.rstrip()
        leading = len(line) - len(line.lstrip())
        if leading == 0:
            got_first_reply = False
            command = line

        if command and leading == 1:
            if not got_first_reply:
                got_first_reply = True
            else:
                print(command)

        print(line)
