# -*- coding: UTF-8 -*-

"""
Module contains an implementation of SONiC Platform Base API and
provides the tlv eeprom information which are available in the platform
"""

try:
    from sonic_platform_base.sonic_eeprom import eeprom_tlvinfo
    from sonic_platform.plat_common import CommonCfg
    from sonic_platform.plat_common import PlatCommon
except ImportError as e:
    raise ImportError(str(e) + "- required module not found") from e


class Eeprom(eeprom_tlvinfo.TlvInfoDecoder):

    def __init__(self):
        super(Eeprom, self).__init__(CommonCfg.S3IP_EEPROM_PATH, 0, '', True)
        self.eeprom_tlv_dict = {}
        self.eeprom_data = None
        self.plat_common = PlatCommon(debug=CommonCfg.DEBUG)

    def _init_eeprom_data(self):
        try:
            self.eeprom_data = self.read_eeprom()
        except:
            self.eeprom_data = "N/A"
            raise RuntimeError("Eeprom is not programed")
        else:
            eeprom = self.eeprom_data
            if self._TLV_HDR_ENABLED:
                if not self.is_valid_tlvinfo_header(eeprom):
                    self.plat_common.log_error('syseeprom tlv header error.')
                    return
                total_len = (eeprom[9] << 8) | eeprom[10]
                tlv_index = self._TLV_INFO_HDR_LEN
                tlv_end = self._TLV_INFO_HDR_LEN + total_len
            else:
                tlv_index = self.eeprom_start
                tlv_end = self._TLV_INFO_MAX_LEN

            while (tlv_index + 2) < len(eeprom) and tlv_index < tlv_end:
                if not self.is_valid_tlv(eeprom[tlv_index:]):
                    self.plat_common.log_error("Invalid TLV field starting at EEPROM offset %d" % tlv_index)
                    break

                tlv = eeprom[tlv_index:tlv_index + 2 + eeprom[tlv_index + 1]]
                code = "0x%02X" % tlv[0]
                _, value = self.decoder(None, tlv)
                self.eeprom_tlv_dict[code] = value

                if eeprom[tlv_index] == self._TLV_CODE_QUANTA_CRC or \
                        eeprom[tlv_index] == self._TLV_CODE_CRC_32:
                    break
                tlv_index += eeprom[tlv_index + 1] + 2

    def get_product_name(self):
        '''
        Returns the value field of the Product Name TLV as a string
        '''
        if not self.eeprom_data:
            self._init_eeprom_data()
        return self.eeprom_tlv_dict.get("0x%02X" % self._TLV_CODE_PRODUCT_NAME, "Undefined.")

    def get_part_number(self):
        '''
        Returns the value field of the Part Number TLV as a string
        '''
        if not self.eeprom_data:
            self._init_eeprom_data()
        return self.eeprom_tlv_dict.get("0x%02X" % self._TLV_CODE_PART_NUMBER, "Undefined.")

    def get_serial_number(self):
        '''
        Returns the value field of the Serial Number TLV as a string
        '''
        if not self.eeprom_data:
            self._init_eeprom_data()
        return self.eeprom_tlv_dict.get("0x%02X" % self._TLV_CODE_SERIAL_NUMBER, "Undefined.")

    def get_base_mac(self):
        '''
        Returns the value field of the MAC #1 Base TLV formatted as a string
        of colon-separated hex digits.
        '''
        if not self.eeprom_data:
            self._init_eeprom_data()
        return self.eeprom_tlv_dict.get("0x%02X" % self._TLV_CODE_MAC_BASE, "Undefined.")

    def get_device_version(self):
        '''
        Returns the value field of the Device Version TLV as a string
        '''
        if not self.eeprom_data:
            self._init_eeprom_data()
        return self.eeprom_tlv_dict.get("0x%02X" % self._TLV_CODE_DEVICE_VERSION, "Undefined.")

    def get_onie_version(self):
        '''
        Returns the value field of the ONIE Version TLV as a string
        '''
        if not self.eeprom_data:
            self._init_eeprom_data()
        return self.eeprom_tlv_dict.get("0x%02X" % self._TLV_CODE_ONIE_VERSION, "Undefined.")

    def get_system_eeprom_info(self):
        '''
        Retrieves the full content of TLV information
        '''
        if not self.eeprom_data:
            self._init_eeprom_data()
        return self.eeprom_tlv_dict
