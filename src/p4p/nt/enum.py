
from __future__ import absolute_import

from ..wrapper import Type, Value
from .common import alarm, timeStamp

class NTEnum(object):
    @staticmethod
    def buildType(extra=[], display=False, control=False, valueAlarm=False):
        F = [
            ('value', ('S', 'enum_t', [
                ('index', 'i'),
                ('choices', 'as'),
            ])),
            ('alarm', alarm),
            ('timeStamp', timeStamp),
        ]
        F.extend(extra)
        return Type(id="epics:nt/NTEnum:1.0", spec=F)

    def __init__(self, **kws):
        self.type = self.buildType(**kws)

    def wrap(self, value):
        """Pack python value into Value
        """
        return Value(self.type, {'value':value})
