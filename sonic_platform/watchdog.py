# -*- coding: UTF-8 -*-

"""
Module contains an implementation of SONiC Platform Base API and
provides the Watchdog information which are available in the platform
"""

try:
    import os.path
    from sonic_platform_base.watchdog_base import WatchdogBase
    from sonic_platform.plat_common import CommonCfg
    from sonic_platform.plat_common import PlatCommon
    from vendor_sonic_platform import hooks
except ImportError as e:
    raise ImportError(str(e) + "- required module not found") from e


class Watchdog(WatchdogBase):

    def __init__(self, has=True):
        self.has_wtd = has
        self.watchdog_path = CommonCfg.S3IP_WTD_PATH
        self.plat_common = PlatCommon(debug=CommonCfg.DEBUG)
        super(Watchdog, self).__init__()

    def get_name(self):
        if self.has_wtd is False:
            return CommonCfg.NULL_VALUE
        file_path = os.path.join(self.watchdog_path, "identify")
        name = self.plat_common.read_file(file_path)
        if self.plat_common.is_valid_value(name):
            return name
        return CommonCfg.NULL_VALUE

    def _enable(self):
        """
        Turn on the watchdog timer
        """
        file_path = os.path.join(self.watchdog_path, "enable")
        return self.plat_common.write_file(file_path, 1)

    def _disable(self):
        """
        Turn off the watchdog timer
        """
        file_path = os.path.join(self.watchdog_path, "enable")
        return self.plat_common.write_file(file_path, 0)

    def _settimeout(self, seconds):
        """
        Set watchdog timer timeout
        """
        file_path = os.path.join(self.watchdog_path, "timeout")
        retval = self.plat_common.write_file(file_path, seconds)
        return retval

    def _gettimeout(self):
        """
        Get watchdog timeout
        """
        file_path = os.path.join(self.watchdog_path, "timeout")
        timeout = self.plat_common.read_file(file_path)
        if self.plat_common.is_valid_value(timeout):
            return int(timeout)
        return None

    def _gettimeleft(self):
        """
        Get time left before watchdog timer expires
        @return time left in seconds
        """
        file_path = os.path.join(self.watchdog_path, "timeleft")
        timeleft = self.plat_common.read_file(file_path)
        if self.plat_common.is_valid_value(timeleft):
            return int(timeleft)
        return None

    def _keepalive(self):
        """
        Keep alive watchdog timer
        """
        if hasattr(hooks, "keep_alive"):
            return hooks.keep_alive()

        square_wave = [1, 0, 1, 0]
        retval = True
        file_path = os.path.join(self.watchdog_path, "reset")

        for val in square_wave:
            retval &= self.plat_common.write_file(file_path, val)

        return retval

    def arm(self, seconds):
        """
        Arm the hardware watchdog with a timeout of <seconds> seconds.
        If the watchdog is currently armed, calling this function will
        simply reset the timer to the provided value. If the underlying
        hardware does not support the value provided in <seconds>, this
        method should arm the watchdog with the *next greater* available
        value.
        Returns:
            An integer specifying the *actual* number of seconds the watchdog
            was armed with. On failure returns -1.
        """
        if self.has_wtd is False:
            return -1

        if seconds < 0:
            return -1

        if self._gettimeout() != seconds:
            retval = self._settimeout(seconds)
            if not retval:
                return -1

        if self.is_armed():
            retval = self._keepalive()
            if not retval:
                return -1
        else:
            retval = self._enable()
            if not retval:
                return -1

        return seconds

    def disarm(self):
        """
        Disarm the hardware watchdog
        Returns:
            A boolean, True if watchdog is disarmed successfully, False if not
        """
        if self.has_wtd is False:
            return False

        if self.is_armed():
            retval = self._disable()
            if not retval:
                return False
            return True

        return True

    def is_armed(self):
        """
        Retrieves the armed state of the hardware watchdog.
        Returns:
            A boolean, True if watchdog is armed, False if not
        """
        if self.has_wtd is False:
            return False

        file_path = os.path.join(self.watchdog_path, "enable")
        armed = self.plat_common.read_file(file_path)
        if not self.plat_common.is_valid_value(armed):
            return False

        if armed == '1':
            return True

        return False

    def get_remaining_time(self):
        """
        If the watchdog is armed, retrieve the number of seconds remaining on
        the watchdog timer
        Returns:
            An integer specifying the number of seconds remaining on thei
            watchdog timer. If the watchdog is not armed, returns -1.
        """
        if self.has_wtd is False:
            return -1

        timeleft = -1

        if self.is_armed():
            try:
                timeleft = self._gettimeleft()
            except IOError:
                pass

        return timeleft
