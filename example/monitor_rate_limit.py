#!/usr/bin/env python
"""For use with 'spamme' from pvAccessCPP in examples/
"""

from __future__ import print_function

import time
import logging
import threading
from functools import partial

try:
    from time import monotonic
except ImportError:
    from time import time as monotonic

from p4p.client.thread import Context
from p4p.util import TimerQueue

def getargs():
    from argparse import ArgumentParser
    A = ArgumentParser()
    A.add_argument('-c', '--count', default=10, type=int)
    A.add_argument('-R', '--rate', default=1.0, type=float)
    A.add_argument('-v', '--verbose', action='store_const', default=logging.INFO, const=logging.DEBUG)
    A.add_argument('pvbase')
    return A.parse_args()

args = getargs()

logging.basicConfig(level=args.verbose)

ctxt = Context('pva')

# callbacks are run from a thread pool, so concurrent prints of different PVs are possible.
iolock = threading.Lock()

TQ = TimerQueue().start()

subs = []

class RateLimit(object):
    def __init__(self, name, period):
        self.name = name
        self.period = period
        self._next_allowed = monotonic() # now

    def __call__(self, V, S):
        now = monotonic()

        if now < self._next_allowed:
            S.pause()
            TQ.push(S.resume, delay=self._next_allowed-now)

        else:
            self._next_allowed = N = now+self.period

        with iolock:
            print(now, self.name, V)

period = 1.0/args.rate

subscriptions = []

for n in range(args.count):
    name = '%s%d'%(args.pvbase, n)
    S = ctxt.monitor(name, RateLimit(name, period), request='record[pipeline=True,queueSize=2]', subscription_arg=True)
    subscriptions.append(S) # must keep Subscription from being collected

try:
    while True:
        time.sleep(1000)
except KeyboardInterrupt:
    pass
finally:
    TQ.join()
