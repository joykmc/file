# -*- coding: UTF-8 -*-

#
# Abstract base class for implementing platform-specific
#  PCIE functionality for SONiC
#
try:
    from sonic_platform_base.sonic_pcie.pcie_common import PcieUtil
except ImportError as e:
    raise ImportError (str(e) + " - required module not found") from e

PATH = "/usr/share/sonic/platform"

class Pcie(PcieUtil):
    def __init__(self, platform_path=PATH):
        PcieUtil.__init__(self, platform_path)
