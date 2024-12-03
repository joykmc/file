# -*- coding: UTF-8 -*-

"""
Module contains an implementation of SONiC Platform Base API and
provides the platform information
"""

try:
    from sonic_platform_base.platform_base import PlatformBase
    from sonic_platform.chassis import Chassis
except ImportError as error:
    raise ImportError(str(error) + "- required module not found") from error


class Platform(PlatformBase):
    """
    Platform-specific Platform class
    """

    def __init__(self):
        PlatformBase.__init__(self)
        self._chassis = Chassis()
