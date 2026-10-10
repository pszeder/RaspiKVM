#!/usr/bin/env python3
"""Bounded release of keyboard/modifier and consumer keys when the application stops."""
import os, select
try:
    fd = os.open("/dev/hidg0", os.O_WRONLY | os.O_NONBLOCK)
    try:
        for report in (bytes([1]) + bytes(8), bytes([2, 0])):
            if select.select([], [fd], [], 0.2)[1]:
                try:
                    os.write(fd, report)
                except OSError:
                    pass
    finally:
        os.close(fd)
except OSError:
    pass
