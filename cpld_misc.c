/*
 * Port Cpld1 driver for SC5631EL-48Y8C-BMC
 *
 * Copyright (C) 2020 Zhang Xixin <zhangxixin@inspur.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/kobject.h>


/* hardware version register */
#define CPLD_HW_VERSION_REG             0x07
/* port cpld write protect reg */
#define CPLD_WP_REG                     0x0c
/* close write protect function */
#define CPLD_WP_CLOSE                   0x59
/* open write protect function */
#define CPLD_WP_OPEN                    0x4e
/* i2c bus read/write times if operate failed */
#define CPLD_RETRY_TIME                 0x03
/* check the port cpld ?exist */
#define VERIFY_REG                      0x04
/* the 0x04 register value */
#define VERIFY_REG_VALUE                0x06
/* CPU_BOARD_CPLD ctrl port power on */
#define ALL_PORT_POWER_ON               0x40
/* PORTx_CPLD enable/disable cpld auto lpmode func */
#define CPLD_AUTO_CTRL_LPMODE           0xf2
/* close write protect */
#define CLOSE_WRITE_PROTECT             0x59
/* open write protect */
#define OPEN_WRITE_PROTECT              0x4e
/* MAC_MISC_CPLD pwr write protect reg */
#define MAC_MISC_CPLD_WP_REG            0x4f

struct cpld_data {
	struct i2c_client *client;
	struct mutex update_lock;
};

struct optoe_platform_data {
	u32		byte_len;		/* size (sum of all addr) */
	u16		page_size;		/* for writes */
	u8		flags;
	void		*dummy1;		/* backward compatibility */
	void		*dummy2;		/* backward compatibility */

#ifdef EEPROM_CLASS
	struct eeprom_platform_data *eeprom_data;
#endif
	char port_name[20];
};

struct optoe_data {
	struct optoe_platform_data chip;
	int use_smbus;
	char port_name[20];

	/*
	 * Lock protects against activities from other Linux tasks,
	 * but not from changes by other I2C masters.
	 */
	struct mutex lock;
	struct bin_attribute bin;
	struct attribute_group attr_group;
	struct attribute_group txdis_attr_group;

	u8 *writebuf;
	unsigned int write_max;

	unsigned int num_addresses;

#ifdef EEPROM_CLASS
	struct eeprom_device *eeprom_dev;
#endif

	/* dev_class: ONE_ADDR (QSFP) or TWO_ADDR (SFP) */
	int dev_class;

	struct i2c_client *client[];
};


struct i2c_client *find_device_by_bus(const char *bus_addr)
{
	struct i2c_client *port_client;
	struct device *i2c_device = NULL;

	i2c_device = bus_find_device_by_name(&i2c_bus_type, NULL, bus_addr);
	if (unlikely(!i2c_device)) {
		pr_err("connot find device: %s\n", bus_addr);
		return (struct i2c_client *)NULL;
	}

	port_client = to_i2c_client(i2c_device);
	return port_client;
}

struct port_id {
	char buf[PLATFORM_NAME_SIZE];
};
static const struct port_id port_bus[] = {
	{"17-000d"},
	{"18-000d"},
	{"19-000d"}
};

/*
 * init_enable_store - control three port cpld function what enable auto enable/disable
 *  cpld control port low power mode; but the port cpld device has three bus, so this
 *  function operate those port cpld.
 * +-------------------+------------+----------+-------------+
 * |     cpld name     |   bus No.  |   addr   |   register  |
 * +-------------------+------------+----------+-------------+
 * |  LC_BOARD_CPLD    |     17     |   0x0d   |    0xf2     |
 * +-------------------+------------+----------+-------------+
 * | MAC_BOARD_CPLD_1  |     18     |   0x0d   |    0xf2     |
 * +-------------------+------------+----------+-------------+
 * | MAC_BOARD_CPLD_2  |     19     |   0x0d   |    0xf2     |
 * +-------------------+------------+----------+-------------+
*/
static ssize_t init_enable_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	s32 ret;
	u8 value;
	size_t idx;
	struct i2c_client *port_client;
	struct cpld_data *data = dev_get_drvdata(dev);

	if(kstrtou8(buf, 0, &value) < 0) {
		dev_err(dev, "invalid written data, please set Dec or Hex(0x..) data.\n");
		return -EINVAL;
	}

	if(value == 1 || value == 0) {
		dev_dbg(dev, "write value is %d\n", value);
	} else {
		dev_err(dev, "write value only support 0 or 1\n");
		return -EINVAL;
	}

	for(idx=0; idx < sizeof(port_bus) / sizeof(*(port_bus+0)); ++idx) {
		port_client = find_device_by_bus((char *)(port_bus+idx));
		if(unlikely(!port_client)) {
			dev_err(dev, "the device(%s) doesn't match.\n", (char *)(port_bus+idx));
			return -EINVAL;
		}

		mutex_lock(&data->update_lock);
		ret = i2c_smbus_read_byte_data(port_client, CPLD_AUTO_CTRL_LPMODE);
		if(unlikely(ret < 0)) {
			dev_err(&port_client->dev, "i2c smbus read failed ret=%d.\n", ret);
			goto SMBUS_READ_ERR;
		}
		dev_dbg(&port_client->dev, "i2c smbus read value(0x%x) from reg(0x%x).\n",
				 ret, CPLD_AUTO_CTRL_LPMODE);

		ret = (value == 1 ? (ret | (1 << 0)) : (ret & ~(1<<0)));
		dev_dbg(&port_client->dev, "i2c smbus will write value(0x%x) into reg(0x%x).\n",
				 ret, CPLD_AUTO_CTRL_LPMODE);

		ret = i2c_smbus_write_byte_data(port_client, CPLD_AUTO_CTRL_LPMODE, value);
		if(ret < 0) {
			dev_err(&port_client->dev, "i2c smbus write(0x%x) into reg(0x%x) failed ret=%d.",
					 value, CPLD_AUTO_CTRL_LPMODE, ret);
			goto SMBUS_WRITE_ERR;
		}

		mutex_unlock(&data->update_lock);
	}
	return count;

SMBUS_READ_ERR:
SMBUS_WRITE_ERR:
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t
init_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	s32 ret;
	struct i2c_client *port_client;
	struct cpld_data *data = dev_get_drvdata(dev);

	port_client = find_device_by_bus((char *)(port_bus+0));
	if(unlikely(!port_client)) {
		dev_err(dev, "the device(%s) doesn't match.\n", (char *)(port_bus+0));
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_read_byte_data(port_client, CPLD_AUTO_CTRL_LPMODE);
	mutex_unlock(&data->update_lock);
	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "i2c smbus read value from reg(0x%02x) failed ret=%d",
				 CPLD_AUTO_CTRL_LPMODE, ret);
		return ret;
	}
	dev_dbg(&port_client->dev, "i2c smbus read value from reg(0x%02x) ret=0x%x",
			 ret, CPLD_AUTO_CTRL_LPMODE);

	return snprintf(buf, PAGE_SIZE, "%d\n", ret & (1<<0));
}

/*
 * all_port_power_on_store - 0x40 register need five bits control all port
 *  power_on, this function do something for 1 can write five bits into 0x40
 *  bit[4:0]
 * +------------------+------------+----------+-------------+
 * |    CPLD NAME     |  BUS No.   |   ADDR   |   REGISTER  |
 * +------------------+------------+----------+-------------+
 * |  MAC_MISC_CPLD   |    214     |   0x31   |    0x40     |
 * +------------------+------------+----------+-------------+
 */
static ssize_t all_port_power_on_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	s32 ret, val;
	u8 value;
	struct i2c_client *port_client;
	struct cpld_data *data = dev_get_drvdata(dev);

	if(kstrtou8(buf, 0, &value) < 0) {
		dev_err(dev, "invalid written data, please set Dec or Hex(0x..) data.\n");
		return -EINVAL;
	}

	if(value == 1 || value == 0) {
		dev_dbg(dev, "written value is %d\n", value);
	} else {
		dev_err(dev, "written value only support 0 or 1\n");
		return -EINVAL;
	}

	port_client = find_device_by_bus("214-0031");
	if(unlikely(!port_client)) {
		dev_err(dev, "the device(%s) doesn't match.\n", "214-0031");
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	val = i2c_smbus_read_byte_data(port_client, ALL_PORT_POWER_ON);
	if(unlikely(val < 0)) {
		dev_err(&port_client->dev, "i2c smbus write failed val=%d.\n", val);
		goto SMBUS_READ_ERR;
	}
	dev_dbg(&port_client->dev, "i2c smbus read value(0x%x) from reg(0x%x).\n",
			 val, ALL_PORT_POWER_ON);

	/* close write protect for reg ctrl */
	ret = i2c_smbus_write_byte_data(port_client, MAC_MISC_CPLD_WP_REG,
			 CLOSE_WRITE_PROTECT);
	dev_dbg(&port_client->dev, "i2c smbus read value(0x%x) from reg(0x%x).\n",
			 i2c_smbus_read_byte_data(port_client, MAC_MISC_CPLD_WP_REG),
			 MAC_MISC_CPLD_WP_REG);

	ret = (value == 1 ? (val | 0x1f) : (val & 0xe0));
	dev_dbg(&port_client->dev, "i2c smbus write value(0x%x) into reg(0x%x).\n",
			 ret, ALL_PORT_POWER_ON);
	ret = i2c_smbus_write_byte_data(port_client, ALL_PORT_POWER_ON, ret);
	if(ret < 0) {
		dev_err(&port_client->dev, "i2c smbus write failed ret=%d.\n", ret);
		goto SMBUS_WRITE_ERR;
	}

	/* open write protect for reg ctrl */
	// ret = i2c_smbus_write_byte_data(port_client, MAC_MISC_CPLD_WP_REG,
	// 		 OPEN_WRITE_PROTECT);

	mutex_unlock(&data->update_lock);

	return count;

SMBUS_READ_ERR:
SMBUS_WRITE_ERR:
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t all_port_power_on_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	s32 ret;
	struct i2c_client *port_client;
	struct cpld_data *data = dev_get_drvdata(dev);

	port_client = find_device_by_bus("214-0031");
	if(unlikely(!port_client)) {
		dev_err(dev, "the device(%s) doesn't match.\n", "214-0031");
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_read_byte_data(port_client, ALL_PORT_POWER_ON);
	mutex_unlock(&data->update_lock);
	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "i2c smbus read reg(0x%02x) failed ret=%d.\n",
				 ALL_PORT_POWER_ON, ret);
		return ret;
	}
	dev_dbg(&port_client->dev, "i2c smbus read value(0x%x) from reg(0x%02x) value.\n",
			 ret, ALL_PORT_POWER_ON);

	return snprintf(buf, PAGE_SIZE, "%d\n", ret & (1<<0));
}

/*************************only for s3ip********************/
/******************************S3IP COLOR****REG COLOR*****/
#define LED_OFF                   0x00       /* 0x0 */
#define LED_GREEN                 0x01       /* 0x4 */
#define LED_YELLOW                0x02       /* 0x6 */
#define LED_RED                   0x03       /* 0x2 */
#define LED_GREEN_AND_BLINK       0x05       /* 0x3 */
#define LED_YELLOW_AND_BLINK      0x06       /* 0x5 */
#define LED_RED_AND_BLINK         0x07       /* 0x1 */
/**************************for s3ip end********************/
#define PORT_LED_CTRL_REG         0x2f

static ssize_t id_led_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	s32 ret;
	u8 value, led_color;
	size_t idx;
	struct i2c_client *port_client;
	struct cpld_data *data = dev_get_drvdata(dev);

	if(kstrtou8(buf, 0, &value) < 0) {
		dev_err(dev, "invalid written data, please set Dec or Hex(0x..) data.\n");
		return -EINVAL;
	}

	switch(value){
		case LED_OFF:
			led_color = 0x0;
			break;
		case LED_GREEN:
			led_color = 0x4;
			break;
		case LED_YELLOW:
			led_color = 0x6;
			break;
		case LED_RED:
			led_color = 0x2;
			break;
		case LED_GREEN_AND_BLINK:
			led_color = 0x3;
			break;
		case LED_YELLOW_AND_BLINK:
			led_color = 0x5;
			break;
		case LED_RED_AND_BLINK:
			led_color = 0x1;
			break;
		default:
			dev_err(dev, "led not support color, invalid written data(%d).\n", value);
			return -EINVAL;;
	}

	for(idx=0; idx < sizeof(port_bus) / sizeof(*(port_bus+0)); ++idx) {
		port_client = find_device_by_bus((char *)(port_bus+idx));
		if(unlikely(!port_client)) {
			dev_err(dev, "the device(%s) doesn't match.\n", (char *)(port_bus+idx));
			return -EINVAL;
		}

		mutex_lock(&data->update_lock);
		ret = i2c_smbus_read_byte_data(port_client, PORT_LED_CTRL_REG);
		if(unlikely(ret < 0)) {
			dev_err(&port_client->dev, "i2c smbus write reg(0x%x) failed ret=%d.\n",
					 PORT_LED_CTRL_REG, ret);
			goto SMBUS_READ_ERR;
		}
		dev_dbg(&port_client->dev, "i2c smbus read value(0x%x) from reg(0x%x).\n",
				 ret, PORT_LED_CTRL_REG);

		value = ret & ~(1<<4);	/* close wp */
		value &= 0xf8;	/* set color */
		value |= led_color;
		dev_dbg(&port_client->dev, "i2c smbus write value(0x%x) into reg(0x%x).\n",
				 value, PORT_LED_CTRL_REG);
		ret = i2c_smbus_write_byte_data(port_client, PORT_LED_CTRL_REG, value);
		if(ret < 0) {
			dev_err(&port_client->dev, "i2c smbus write failed ret=%d", ret);
			goto SMBUS_WRITE_ERR;
		}
#if 0
		value |= (1<<4);	/* open wp */
		dev_dbg(&port_client->dev, "i2c smbus write value(0x%x) into reg(0x%x).\n",
				 value, PORT_LED_CTRL_REG);
		ret = i2c_smbus_write_byte_data(port_client, PORT_LED_CTRL_REG, value);
		if(ret < 0) {
			dev_err(&port_client->dev, "i2c smbus write failed ret=%d", ret);
			goto SMBUS_WRITE_ERR;
		}
#endif
		mutex_unlock(&data->update_lock);
	}
	return count;

SMBUS_READ_ERR:
SMBUS_WRITE_ERR:
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t id_led_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	s32 ret;
	u8 value, led_color;
	struct i2c_client *port_client;
	struct cpld_data *data = dev_get_drvdata(dev);

	port_client = find_device_by_bus((char *)(port_bus+0));
	if(unlikely(!port_client)) {
		dev_err(dev, "the device(%s) doesn't match.\n", (char *)(port_bus+0));
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_read_byte_data(port_client, PORT_LED_CTRL_REG);
	mutex_unlock(&data->update_lock);
	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "i2c smbus read reg(0x%02x) failed ret=%d,\n",
				 PORT_LED_CTRL_REG, ret);
		return ret;
	}

	dev_dbg(&port_client->dev, "i2c smbus read value(0x%x) from reg(0x%02x).\n",
			 ret, PORT_LED_CTRL_REG);
	value = ret & 0x7;
	switch(value){
		case 0x0:
			led_color = LED_OFF;
			break;
		case 0x4:
			led_color = LED_GREEN;
			break;
		case 0x6:
			led_color = LED_YELLOW;
			break;
		case 0x2:
			led_color = LED_RED;
			break;
		case 0x3:
			led_color = LED_GREEN_AND_BLINK;
			break;
		case 0x5:
			led_color = LED_YELLOW_AND_BLINK;
			break;
		case 0x1:
			led_color = LED_RED_AND_BLINK;
			break;
		default:
			dev_err(dev, "led not support this color, invalid written data(%d).\n", value);
			return -EINVAL;;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", led_color);
}

static ssize_t id_led_mode_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	s32 ret;
	u8 value, mode;
	size_t idx;
	struct i2c_client *port_client;
	struct cpld_data *data = dev_get_drvdata(dev);

	if(kstrtou8(buf, 0, &value) < 0) {
		dev_err(dev, "invalid written data, please set Dec or Hex(0x..) data.\n");
		return -EINVAL;
	}

	if(value == 0 || value == 1) {
		dev_dbg(&port_client->dev, "set mode: %s.\n",
				 value == 1 ? "close write protect": "open write protect");
	} else {
		dev_err(dev, "invalid written data, only support 0 or 1, but input: %s.\n", buf);
		return -EINVAL;
	}

	for(idx=0; idx < sizeof(port_bus) / sizeof(*(port_bus+0)); ++idx) {
		port_client = find_device_by_bus((char *)(port_bus+idx));
		if(unlikely(!port_client)) {
			dev_err(dev, "the device(%s) doesn't match.\n", (char *)(port_bus+idx));
			return -EINVAL;
		}

		mutex_lock(&data->update_lock);
		ret = i2c_smbus_read_byte_data(port_client, PORT_LED_CTRL_REG);
		if(unlikely(ret < 0)) {
			dev_err(&port_client->dev, "i2c smbus write reg(0x%x) failed ret=%d.\n",
					 PORT_LED_CTRL_REG, ret);
			goto SMBUS_READ_ERR;
		}
		dev_dbg(&port_client->dev, "i2c smbus read value(0x%x) from reg(0x%x).\n",
				 ret, PORT_LED_CTRL_REG);

		/* value==1: close wp; value==0: open wp */
		mode = (value == 1 ? ret | (1<<4) : ret & ~(1<<4));

		dev_dbg(&port_client->dev, "i2c smbus write value(0x%x) into reg(0x%x).\n",
				 value, PORT_LED_CTRL_REG);

		ret = i2c_smbus_write_byte_data(port_client, PORT_LED_CTRL_REG, mode);
		if(ret < 0) {
			dev_err(&port_client->dev, "i2c smbus write failed ret=%d", ret);
			goto SMBUS_WRITE_ERR;
		}

		mutex_unlock(&data->update_lock);
	}
	return count;

SMBUS_READ_ERR:
SMBUS_WRITE_ERR:
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t id_led_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	s32 ret;
	u8 mode;
	struct i2c_client *port_client;
	struct cpld_data *data = dev_get_drvdata(dev);

	port_client = find_device_by_bus((char *)(port_bus+0));
	if(unlikely(!port_client)) {
		dev_err(dev, "the device(%s) doesn't match.\n", (char *)(port_bus+0));
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_read_byte_data(port_client, PORT_LED_CTRL_REG);
	mutex_unlock(&data->update_lock);
	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "i2c smbus read reg(0x%02x) failed ret=%d,\n",
				 PORT_LED_CTRL_REG, ret);
		return ret;
	}

	mode = (ret >> 4) & 1;
	dev_dbg(&port_client->dev, "i2c smbus read value(0x%x) from reg(0x%02x), mode=%d.\n",
			 ret, PORT_LED_CTRL_REG, mode);

	return snprintf(buf, PAGE_SIZE, "%d\n", mode);
}

/*
 * port_power_on_store - 0x40 register need five bits control all port
 *  power_on, this function do something for 1 can write five bits into 0x40
 *  bit[4:0]
 * this function for control single port, and not really single port.
 * such as:
 * +------------------+------------+----------+-------------+-----+----------------+
 * |    CPLD NAME     |  BUS No.   |   ADDR   |   REGISTER  | bit |      PORT      |
 * +------------------+------------+----------+-------------+-----+----------------+
 * |  MAC_MISC_CPLD   |    214     |   0x31   |    0x40     |  0  | MAC1 front  22 |
 * +------------------+------------+----------+-------------+-----+----------------+
 * |  MAC_MISC_CPLD   |    214     |   0x31   |    0x40     |  1  | MAC1 behind 22 |
 * +------------------+------------+----------+-------------+-----+----------------+
 * |  MAC_MISC_CPLD   |    214     |   0x31   |    0x40     |  2  | MAC2 front  22 |
 * +------------------+------------+----------+-------------+-----+----------------+
 * |  MAC_MISC_CPLD   |    214     |   0x31   |    0x40     |  3  | MAC2 behind 22 |
 * +------------------+------------+----------+-------------+-----+----------------+
 * |  MAC_MISC_CPLD   |    214     |   0x31   |    0x40     |  4  |   LC  ALL 40   |
 * +------------------+------------+----------+-------------+-----+----------------+
 *
 * +------------------+------------+----------+-------------+
 * |    CPLD NAME     |  BUS No.   |   ADDR   |   REGISTER  |
 * +------------------+------------+----------+-------------+
 * |  MAC_MISC_CPLD   |    214     |   0x31   |    0x40     |
 * +------------------+------------+----------+-------------+
 */
static ssize_t port_power_on_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	s32 ret, val;
	u8 value;
	struct i2c_client *port_client;
	struct cpld_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);

	if(kstrtou8(buf, 0, &value) < 0) {
		dev_err(dev, "invalid written data, please set Dec or Hex(0x..) data.\n");
		return -EINVAL;
	}

	if(value == 1 || value == 0) {
		dev_dbg(dev, "written value is %d\n", value);
	} else {
		dev_err(dev, "written value only support 0 or 1\n");
		return -EINVAL;
	}

	port_client = find_device_by_bus("214-0031");
	if(unlikely(!port_client)) {
		dev_err(dev, "the device(%s) doesn't match.\n", "214-0031");
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	val = i2c_smbus_read_byte_data(port_client, ALL_PORT_POWER_ON);
	if(unlikely(val < 0)) {
		dev_err(&port_client->dev, "i2c smbus write failed ret=%d.\n", val);
		goto SMBUS_READ_ERR;
	}
	dev_dbg(&port_client->dev, "i2c smbus read value(0x%x) from reg(0x%x), "
			"and will change bit[%d].\n", val, ALL_PORT_POWER_ON, sda->index);

	/* close write protect for reg ctrl */
	ret = i2c_smbus_write_byte_data(port_client, MAC_MISC_CPLD_WP_REG,
			 CLOSE_WRITE_PROTECT);
	dev_dbg(&port_client->dev, "i2c smbus read value(0x%x) from reg(0x%x).\n",
			 i2c_smbus_read_byte_data(port_client, MAC_MISC_CPLD_WP_REG),
			 MAC_MISC_CPLD_WP_REG);

	ret = (value == 1 ? (val | (1 << sda->index)) : (val & ~(1 << sda->index)));
	dev_dbg(&port_client->dev, "i2c smbus write value(0x%x) into reg(0x%x).\n",
			 ret, ALL_PORT_POWER_ON);
	ret = i2c_smbus_write_byte_data(port_client, ALL_PORT_POWER_ON, ret);
	if(ret < 0) {
		dev_err(&port_client->dev, "i2c smbus write failed ret=%d.\n", ret);
		goto SMBUS_WRITE_ERR;
	}

	/* open write protect for reg ctrl */
	// ret = i2c_smbus_write_byte_data(port_client, MAC_MISC_CPLD_WP_REG,
	// 		 OPEN_WRITE_PROTECT);

	mutex_unlock(&data->update_lock);

	return count;

SMBUS_READ_ERR:
SMBUS_WRITE_ERR:
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t port_power_on_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	s32 ret;
	struct i2c_client *port_client;
	struct cpld_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);

	port_client = find_device_by_bus("214-0031");
	if(unlikely(!port_client)) {
		dev_err(dev, "the device(%s) doesn't match.\n", "214-0031");
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_read_byte_data(port_client, ALL_PORT_POWER_ON);
	mutex_unlock(&data->update_lock);
	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "i2c smbus read reg(0x%02x) failed ret=%d.\n",
				 ALL_PORT_POWER_ON, ret);
		return ret;
	}
	dev_dbg(&port_client->dev, "i2c smbus read value(0x%x) from reg(0x%02x) value.\n",
			 ret, ALL_PORT_POWER_ON);

	return snprintf(buf, PAGE_SIZE, "%d\n", (ret >> sda->index) & 1);
}
struct page_reg {
	u8 page;
	u8 reg;
};

struct ctrl_reg {
	struct page_reg rx_los;
	struct page_reg tx_fault;
	struct page_reg tx_disable;
};

static const struct ctrl_reg _info[] = {
	{	/* cmis5p1 -> byte */
		{0x11, 0x93},	/* Rx_LOS Page 180*/
		{0x11, 0x87},	/* Tx_Fault Page 178 */
		{0x10, 0x82}	/* Tx_Disable Page 162 */
	},
	{	/* sff8636 -> only bit0 bit1 bit2 bit4 */
		{0x00, 0x03},	/* Rx_LOS Page 34 */
		{0x00, 0x04},	/* Tx_Fault Page 34 */
		{0x00, 0x56}	/* Tx_Disable Page 38 Tx1 Tx2 Tx3 Tx4 */
	}
};

/* This struct for physical info. The information is already fixed
 * when the machine produces it.
 * */
struct electric_phys_chara {
	/* for ctrl/read rx_los tx_fault tx_disable */
	u8 port;
	char bus[10];
	/* for find Prs info */
	u8 cpld_bus;
	u8 cpld_addr;
	u8 cpld_reg;
	u8 mask;
};

static const struct electric_phys_chara port_info[] = {
	{ 0 /* only start */ },
	{1, "23-0050", 18, 0x0d, 0x30, 0x01},
	{2, "24-0050", 18, 0x0d, 0x30, 0x02},
	{3, "25-0050", 18, 0x0d, 0x30, 0x04},
	{4, "26-0050", 18, 0x0d, 0x30, 0x08},
	{5, "111-0050", 17, 0x0d, 0x30, 0x01},
	{6, "112-0050", 17, 0x0d, 0x30, 0x02},	/* LC PORT */

	{7, "27-0050", 18, 0x0d, 0x30, 0x10},
	{8, "28-0050", 18, 0x0d, 0x30, 0x20},
	{9, "29-0050", 18, 0x0d, 0x30, 0x40},
	{10, "30-0050", 18, 0x0d, 0x30, 0x80},
	{11, "113-0050", 17, 0x0d, 0x30, 0x04},
	{12, "114-0050", 17, 0x0d, 0x30, 0x08},	/* LC PORT */

	{13, "31-0050", 18, 0x0d, 0x31, 0x01},
	{14, "32-0050", 18, 0x0d, 0x31, 0x02},
	{15, "33-0050", 18, 0x0d, 0x31, 0x04},
	{16, "34-0050", 18, 0x0d, 0x31, 0x08},
	{17, "115-0050", 17, 0x0d, 0x30, 0x10},
	{18, "116-0050", 17, 0x0d, 0x30, 0x20},	/* LC PORT */

	{19, "35-0050", 18, 0x0d, 0x31, 0x10},
	{20, "36-0050", 18, 0x0d, 0x31, 0x20},
	{21, "37-0050", 18, 0x0d, 0x31, 0x40},
	{22, "38-0050", 18, 0x0d, 0x31, 0x80},
	{23, "118-0050", 17, 0x0d, 0x30, 0x40},
	{24, "118-0050", 17, 0x0d, 0x30, 0x80},	/* LC PORT */

	{25, "39-0050", 18, 0x0d, 0x32, 0x01},
	{26, "40-0050", 18, 0x0d, 0x32, 0x02},
	{27, "41-0050", 18, 0x0d, 0x32, 0x04},
	{28, "42-0050", 18, 0x0d, 0x32, 0x08},
	{29, "119-0050", 17, 0x0d, 0x31, 0x01},
	{30, "120-0050", 17, 0x0d, 0x31, 0x02},	/* LC PORT */

	{31, "43-0050", 18, 0x0d, 0x32, 0x10},
	{32, "44-0050", 18, 0x0d, 0x32, 0x20},
	{33, "45-0050", 18, 0x0d, 0x32, 0x40},
	{34, "46-0050", 18, 0x0d, 0x32, 0x80},
	{35, "121-0050", 17, 0x0d, 0x31, 0x04},
	{36, "122-0050", 17, 0x0d, 0x31, 0x08},	/* LC PORT */

	{37, "47-0050", 18, 0x0d, 0x33, 0x01},
	{38, "48-0050", 18, 0x0d, 0x33, 0x02},
	{39, "49-0050", 18, 0x0d, 0x33, 0x04},
	{40, "50-0050", 18, 0x0d, 0x33, 0x08},
	{41, "123-0050", 17, 0x0d, 0x31, 0x10},
	{42, "124-0050", 17, 0x0d, 0x31, 0x20},	/* LC PORT */

	{43, "51-0050", 18, 0x0d, 0x33, 0x10},
	{44, "52-0050", 18, 0x0d, 0x33, 0x20},
	{45, "53-0050", 18, 0x0d, 0x33, 0x40},
	{46, "54-0050", 18, 0x0d, 0x33, 0x80},
	{47, "125-0050", 17, 0x0d, 0x31, 0x40},
	{48, "126-0050", 17, 0x0d, 0x31, 0x80},	/* LC PORT */

	{49, "55-0050", 18, 0x0d, 0x34, 0x01},
	{50, "56-0050", 18, 0x0d, 0x34, 0x02},
	{51, "57-0050", 18, 0x0d, 0x34, 0x04},
	{52, "58-0050", 18, 0x0d, 0x34, 0x08},
	{53, "127-0050", 17, 0x0d, 0x32, 0x01},
	{54, "128-0050", 17, 0x0d, 0x32, 0x02},	/* LC PORT */

	{55, "59-0050", 18, 0x0d, 0x34, 0x10},
	{56, "60-0050", 18, 0x0d, 0x34, 0x20},
	{57, "61-0050", 18, 0x0d, 0x34, 0x40},
	{58, "62-0050", 18, 0x0d, 0x34, 0x80},
	{59, "129-0050", 17, 0x0d, 0x32, 0x04},
	{60, "130-0050", 17, 0x0d, 0x32, 0x08},	/* LC PORT */

	{61, "63-0050", 18, 0x0d, 0x35, 0x01},
	{62, "64-0050", 18, 0x0d, 0x35, 0x02},
	{63, "65-0050", 18, 0x0d, 0x35, 0x04},
	{64, "66-0050", 18, 0x0d, 0x35, 0x08},	/* <-- SMB PORT1 CPLD END */
	{65, "131-0050", 17, 0x0d, 0x32, 0x10},
	{66, "132-0050", 17, 0x0d, 0x32, 0x20},	/* LC PORT */

	{67, "67-0050", 19, 0x0d, 0x30, 0x01},	/* --> SMB PORT2 CPLD START */
	{68, "68-0050", 19, 0x0d, 0x30, 0x02},
	{69, "69-0050", 19, 0x0d, 0x30, 0x04},
	{70, "70-0050", 19, 0x0d, 0x30, 0x08},
	{71, "133-0050", 17, 0x0d, 0x32, 0x40},
	{72, "134-0050", 17, 0x0d, 0x32, 0x80},	/* LC PORT */

	{73, "71-0050", 19, 0x0d, 0x30, 0x10},
	{74, "72-0050", 19, 0x0d, 0x30, 0x20},
	{75, "73-0050", 19, 0x0d, 0x30, 0x40},
	{76, "74-0050", 19, 0x0d, 0x30, 0x80},
	{77, "135-0050", 17, 0x0d, 0x33, 0x01},
	{78, "136-0050", 17, 0x0d, 0x33, 0x02},	/* LC PORT */

	{79, "75-0050", 19, 0x0d, 0x31, 0x01},
	{80, "76-0050", 19, 0x0d, 0x31, 0x02},
	{81, "77-0050", 19, 0x0d, 0x31, 0x04},
	{82, "78-0050", 19, 0x0d, 0x31, 0x08},
	{83, "137-0050", 17, 0x0d, 0x33, 0x04},
	{84, "138-0050", 17, 0x0d, 0x33, 0x08},	/* LC PORT */

	{85, "79-0050", 19, 0x0d, 0x31, 0x10},
	{86, "80-0050", 19, 0x0d, 0x31, 0x20},
	{87, "81-0050", 19, 0x0d, 0x31, 0x40},
	{88, "82-0050", 19, 0x0d, 0x31, 0x80},
	{89, "139-0050", 17, 0x0d, 0x33, 0x10},
	{90, "140-0050", 17, 0x0d, 0x33, 0x20},	/* LC PORT */

	{91, "83-0050", 19, 0x0d, 0x32, 0x01},
	{92, "84-0050", 19, 0x0d, 0x32, 0x02},
	{93, "85-0050", 19, 0x0d, 0x32, 0x04},
	{94, "86-0050", 19, 0x0d, 0x32, 0x08},
	{95, "141-0050", 17, 0x0d, 0x33, 0x40},
	{96, "142-0050", 17, 0x0d, 0x33, 0x80},	/* LC PORT */

	{97, "87-0050", 19, 0x0d, 0x32, 0x10},
	{98, "88-0050", 19, 0x0d, 0x32, 0x20},
	{99, "89-0050", 19, 0x0d, 0x32, 0x40},
	{100, "90-0050", 19, 0x0d, 0x32, 0x80},
	{101, "143-0050", 17, 0x0d, 0x34, 0x01},
	{102, "144-0050", 17, 0x0d, 0x34, 0x02},	/* LC PORT */

	{103, "91-0050", 19, 0x0d, 0x33, 0x01},
	{104, "92-0050", 19, 0x0d, 0x33, 0x02},
	{105, "93-0050", 19, 0x0d, 0x33, 0x04},
	{106, "94-0050", 19, 0x0d, 0x33, 0x08},
	{107, "145-0050", 17, 0x0d, 0x34, 0x04},
	{108, "146-0050", 17, 0x0d, 0x34, 0x08},	/* LC PORT */

	{109, "95-0050", 19, 0x0d, 0x33, 0x10},
	{110, "96-0050", 19, 0x0d, 0x33, 0x20},
	{111, "97-0050", 19, 0x0d, 0x33, 0x40},
	{112, "98-0050", 19, 0x0d, 0x33, 0x80},
	{113, "147-0050", 17, 0x0d, 0x34, 0x10},
	{114, "148-0050", 17, 0x0d, 0x34, 0x20},	/* LC PORT */

	{115, "99-0050", 19, 0x0d, 0x34, 0x01},
	{116, "100-0050", 19, 0x0d, 0x34, 0x02},
	{117, "101-0050", 19, 0x0d, 0x34, 0x04},
	{118, "102-0050", 19, 0x0d, 0x34, 0x08},
	{119, "149-0050", 17, 0x0d, 0x34, 0x40},
	{120, "150-0050", 17, 0x0d, 0x34, 0x80},	/* LC PORT */

	{121, "103-0050", 19, 0x0d, 0x34, 0x10},
	{122, "104-0050", 19, 0x0d, 0x34, 0x20},
	{123, "105-0050", 19, 0x0d, 0x34, 0x40},
	{124, "106-0050", 19, 0x0d, 0x34, 0x80},

	{125, "107-0050", 19, 0x0d, 0x35, 0x01},
	{126, "108-0050", 19, 0x0d, 0x35, 0x02},
	{127, "109-0050", 19, 0x0d, 0x35, 0x04},
	{128, "110-0050", 19, 0x0d, 0x35, 0x08},
};

#define AOC_TYPE_REG			0x00
#define SFF8636_TYPE_VAL		0x11
#define CMIS5P1_TYPE_VAL		0x1e
#define SFF8636_TYPE			0x01
#define CMIS5P1_TYPE			0x00
#define SELECT_PAGE_REG			0x7f

static u8 get_port_prs_sta(u8 index)
{
	s32 _val;
	u8 _reg, _mask, _bus, _addr;
	char device[10] = {0};
	struct i2c_client *port_client;

	_bus = (port_info + index)->cpld_bus;
	_addr = (port_info + index)->cpld_addr;
	_reg = (port_info + index)->cpld_reg;
	_mask = (port_info + index)->mask;
	snprintf(device, sizeof("ddd-00xx"), "%d-00%02x", _bus, _addr);

	pr_debug("enter %s, index=%d, _bus=0x%02x, _addr=0x%02x, _reg=0x%02x, _mask=0x%02x, device=%s\n",
			__func__, index, _bus, _addr, _reg, _mask, device);

	port_client = find_device_by_bus(device);
	if(unlikely(!port_client)) {
		pr_err("the device(%s) doesn't match.\n", device);
		return -EINVAL;
	}

	_val = i2c_smbus_read_byte_data(port_client, _reg);
	if(unlikely(_val < 0)) {
		dev_err(&port_client->dev, "get eth%d port_prs_sta, read reg(0x%02x) failed ret=%d.\n",
				 index, AOC_TYPE_REG, _val);
		return _val;
	}
	dev_dbg(&port_client->dev, "get eth%d port_prs_sta, status=0x%x.\n",
			 index, (~_val) & 0x1);

	return _val & _mask;
}


static ssize_t tx_fault_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	s32 type;
	s32 ret;
	u8 page, reg, _val;
	struct i2c_client *port_client;
	struct optoe_data *optoe_data;
	struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);

	dev_dbg(dev, "enter %s, sda->index=%d, bus=%s", __func__, sda->index,
			 (port_info + sda->index)->bus);

	if(get_port_prs_sta(sda->index) == 1) {
		dev_err(dev, "the device(eth%d) doesn't found.\n", sda->index);
		return -ENODEV;
	}

	port_client = find_device_by_bus((port_info + sda->index)->bus);
	if(unlikely(!port_client)) {
		dev_err(dev, "the device(%s) doesn't match.\n", (port_info + sda->index)->bus);
		return -EINVAL;
	}

	optoe_data = i2c_get_clientdata(port_client);
	if (!optoe_data) {
		dev_err(&port_client->dev, "optoe client optoe_data do not find\n");
		return -ENODEV;
	}

	ret = i2c_smbus_read_byte_data(port_client, AOC_TYPE_REG);
	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "get eth%d tx_fault, read reg(0x%02x) failed ret=%d.\n",
				 sda->index, AOC_TYPE_REG, ret);
		return ret;
	}
	dev_dbg(&port_client->dev, "get eth%d tx_fault, read aoc type(0x%x) from reg(0x%02x) value.\n",
			 sda->index, ret, AOC_TYPE_REG);

	/* other value is error */
	if(unlikely(ret != SFF8636_TYPE_VAL && ret != CMIS5P1_TYPE_VAL)) {
		dev_info(&port_client->dev, "this device is not recognized.\n");
		return -ENODEV;
	}

	type = (ret == SFF8636_TYPE_VAL ? SFF8636_TYPE : CMIS5P1_TYPE);
	page = (_info + type)->tx_fault.page;
	reg = (_info + type)->tx_fault.reg;
	dev_dbg(&port_client->dev, "get eth%d tx_fault, type=%d, page=0x%02x, reg=0x%02x.\n",
			 sda->index, type, page, reg);

	/* save the original page */
	_val = i2c_smbus_read_byte_data(port_client, SELECT_PAGE_REG);
	if(unlikely(_val < 0)) {
		dev_err(&port_client->dev, "get eth%d tx_fault, read reg(0x%02x) failed _val=%d.\n",
				 sda->index, SELECT_PAGE_REG, _val);
		return _val;
	}
	dev_dbg(&port_client->dev, "get eth%d tx_fault, read value(0x%x) from reg(0x%02x) value.\n",
			 sda->index, _val, SELECT_PAGE_REG);

	mutex_lock(&optoe_data->lock);
	ret = i2c_smbus_write_byte_data(port_client, SELECT_PAGE_REG, page);
	dev_dbg(&port_client->dev, "get eth%d tx_fault, set value(0x%02x) into reg(0x%02x).\n",
			 sda->index, i2c_smbus_read_byte_data(port_client, SELECT_PAGE_REG),
			 SELECT_PAGE_REG);

	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "get eth%d tx_fault failed, err code(%d).\n", sda->index, ret);
		mutex_unlock(&optoe_data->lock);
		return ret;
	}

	ret = i2c_smbus_read_byte_data(port_client, reg);
	if(unlikely(ret < 0)) {
		mutex_unlock(&optoe_data->lock);
		return ret;
	}
	dev_dbg(&port_client->dev, "get eth%d tx_fault, read reg(0x%02x) value(%d).\n",
				sda->index, reg, ret);

	i2c_smbus_write_byte_data(port_client, SELECT_PAGE_REG, _val);
	mutex_unlock(&optoe_data->lock);

	return snprintf(buf, PAGE_SIZE, "0x%02x\n", type == SFF8636_TYPE ? (ret & 0x0f) : ret);
}


static ssize_t tx_disable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	s32 type;
	s32 ret;
	u8 page, reg, _val;
	struct i2c_client *port_client;
	struct optoe_data *optoe_data;
	struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);

	dev_dbg(dev, "enter %s, sda->index=%d, bus=%s", __func__, sda->index,
			 (port_info + sda->index)->bus);

	if(get_port_prs_sta(sda->index) == 1) {
		dev_err(dev, "the device(eth%d) doesn't found.\n", sda->index);
		return -ENODEV;
	}

	port_client = find_device_by_bus((port_info + sda->index)->bus);
	if(unlikely(!port_client)) {
		dev_err(dev, "the device(%s) doesn't match.\n", (port_info + sda->index)->bus);
		return -EINVAL;
	}

	optoe_data = i2c_get_clientdata(port_client);
	if (!optoe_data) {
		dev_err(&port_client->dev, "optoe client optoe_data do not find\n");
		return -ENODEV;
	}

	ret = i2c_smbus_read_byte_data(port_client, AOC_TYPE_REG);
	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "get eth%d tx_disable, read reg(0x%02x) failed ret=%d.\n",
				 sda->index, AOC_TYPE_REG, ret);
		return ret;
	}
	dev_dbg(&port_client->dev, "get eth%d tx_disable, read aoc type(0x%x) from reg(0x%02x) value.\n",
			 sda->index, ret, AOC_TYPE_REG);

	/* other value is error */
	if(ret != SFF8636_TYPE_VAL && ret != CMIS5P1_TYPE_VAL ) {
		dev_info(&port_client->dev, "this device is not recognized.\n");
		return -ENODEV;
	}

	type = (ret == SFF8636_TYPE_VAL ? SFF8636_TYPE : CMIS5P1_TYPE);
	page = (_info + type)->tx_disable.page;
	reg = (_info + type)->tx_disable.reg;
	dev_dbg(&port_client->dev, "get eth%d tx_fault, type=%d, page=0x%02x, reg=0x%02x.\n",
			 sda->index, type, page, reg);

	/* save the original page */
	_val = i2c_smbus_read_byte_data(port_client, SELECT_PAGE_REG); // is page???
	if(unlikely(_val < 0)) {
		dev_err(&port_client->dev, "get eth%d tx_disable, read reg(0x%02x) failed _val=%d.\n",
				 sda->index, AOC_TYPE_REG, _val);
		return _val;
	}
	dev_dbg(&port_client->dev, "get eth%d tx_disable, read value(0x%x) from reg(0x%02x) value.\n",
			 sda->index, _val, AOC_TYPE_REG);

	mutex_lock(&optoe_data->lock);
	ret = i2c_smbus_write_byte_data(port_client, SELECT_PAGE_REG, page);
	dev_dbg(&port_client->dev, "get eth%d tx_disable, set value(0x%02x) into reg(0x%02x).\n",
			 sda->index, i2c_smbus_read_byte_data(port_client, SELECT_PAGE_REG),
			 SELECT_PAGE_REG);

	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "get eth%d tx_disable failed, err code(%d).\n", sda->index, ret);
		mutex_unlock(&optoe_data->lock);
		return ret;
	}

	ret = i2c_smbus_read_byte_data(port_client, reg);
	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "get eth%d tx_disable failed, read reg(0x%02x) failed code(%d).\n",
				 sda->index, reg, ret);
		mutex_unlock(&optoe_data->lock);
		return ret;
	}

	i2c_smbus_write_byte_data(port_client, SELECT_PAGE_REG, _val);
	mutex_unlock(&optoe_data->lock);

	return snprintf(buf, PAGE_SIZE, "0x%02x\n", type == SFF8636_TYPE ? (ret & 0x0f) : ret);
}

static ssize_t tx_disable_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	s32 type;
	s32 ret;
	u8 page, reg, _val, value, get_value;
	struct i2c_client *port_client;
	struct optoe_data *optoe_data;
	struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);

	dev_dbg(dev, "enter %s, sda->index=%d, bus=%s", __func__, sda->index,
			 (port_info + sda->index)->bus);

	if(get_port_prs_sta(sda->index) == 1) {
		dev_err(dev, "the device(eth%d) doesn't found.\n", sda->index);
		return -ENODEV;
	}

	port_client = find_device_by_bus((port_info + sda->index)->bus);
	if(unlikely(!port_client)) {
		dev_err(dev, "the device(%s) doesn't match.\n", (port_info + sda->index)->bus);
		return -EINVAL;
	}

	optoe_data = i2c_get_clientdata(port_client);
	if (!optoe_data) {
		dev_err(&port_client->dev, "optoe client optoe_data do not find\n");
		return -ENODEV;
	}

	ret = i2c_smbus_read_byte_data(port_client, AOC_TYPE_REG);
	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "get eth%d tx_disable, read reg(0x%02x) "
				 "failed ret=%d.\n", sda->index, AOC_TYPE_REG, ret);
		return ret;
	}
	dev_dbg(&port_client->dev, "get eth%d tx_disable, read aoc type(0x%x) from "
			 "reg(0x%02x) value.\n", sda->index, ret, AOC_TYPE_REG);

	/* other value is error */
	if(ret != SFF8636_TYPE_VAL && ret != CMIS5P1_TYPE_VAL ) {
		dev_info(&port_client->dev, "this device is not recognized.\n");
		return -ENODEV;
	}

	if(kstrtou8(buf, 0, &value) < 0) {
		dev_err(dev, "invalid written data, please set Dec or Hex(0x..) data.\n");
		return -EINVAL;
	}

	type = (ret == SFF8636_TYPE_VAL ? SFF8636_TYPE : CMIS5P1_TYPE);
	page = (_info + type)->tx_disable.page;
	reg = (_info + type)->tx_disable.reg;
	dev_dbg(&port_client->dev, "get eth%d tx_fault, type=%d, page=0x%02x, reg=0x%02x.\n",
			 sda->index, type, page, reg);

	/* save the original page */
	_val = i2c_smbus_read_byte_data(port_client, SELECT_PAGE_REG);
	if(unlikely(_val < 0)) {
		dev_err(&port_client->dev, "get eth%d tx_disable, read reg(0x%02x) failed _val=%d.\n",
				 sda->index, SELECT_PAGE_REG, _val);
		return _val;
	}
	dev_dbg(&port_client->dev, "get eth%d tx_disable, read value(0x%x) from reg(0x%02x) value.\n",
			 sda->index, _val, SELECT_PAGE_REG);

	/* select page */
	mutex_lock(&optoe_data->lock);
	ret = i2c_smbus_write_byte_data(port_client, SELECT_PAGE_REG, page);
	dev_dbg(&port_client->dev, "get eth%d tx_disable: select page, set value(0x%02x) into reg(0x%02x).\n",
			 sda->index, i2c_smbus_read_byte_data(port_client, SELECT_PAGE_REG),
			 SELECT_PAGE_REG);

	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "get eth%d tx_disable failed, err code(%d).\n", sda->index, ret);
		mutex_unlock(&optoe_data->lock);
		return ret;
	}

	/* set tx_disable */
	ret = i2c_smbus_write_byte_data(port_client, reg, value);

	dev_info(&port_client->dev, "get eth%d tx_disable, set value (0x%02x) into reg(0x%02x) get value ((0x%02x)), ret=%d, count=%ld.\n",
			 sda->index, value, reg, i2c_smbus_read_byte_data(port_client, reg), ret, count);

	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "get eth%d tx_disable failed, err code(%d).\n", sda->index, ret);
		mutex_unlock(&optoe_data->lock);
		return ret;
	}

	i2c_smbus_write_byte_data(port_client, SELECT_PAGE_REG, _val);
	mutex_unlock(&optoe_data->lock);

	return count;
}


static ssize_t rx_los_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	s32 type;
	s32 ret;
	u8 page, reg, _val;
	struct i2c_client *port_client;
	struct optoe_data *optoe_data;
	struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);

	dev_dbg(dev, "enter %s, sda->index=%d, bus=%s", __func__, sda->index,
			 (port_info + sda->index)->bus);

	if(get_port_prs_sta(sda->index) == 1) {
		dev_err(dev, "the device(eth%d) doesn't found.\n", sda->index);
		return -ENODEV;
	}

	port_client = find_device_by_bus((port_info + sda->index)->bus);
	if(unlikely(!port_client)) {
		dev_err(dev, "the device(%s) doesn't match.\n", (port_info + sda->index)->bus);
		return -EINVAL;
	}

	optoe_data = i2c_get_clientdata(port_client);
	if (!optoe_data) {
		dev_err(&port_client->dev, "optoe client optoe_data do not find\n");
		return -ENODEV;
	}

	ret = i2c_smbus_read_byte_data(port_client, AOC_TYPE_REG);
	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "set eth%d rx_los, read reg(0x%02x) failed ret=%d.\n",
				 sda->index, AOC_TYPE_REG, ret);
		return ret;
	}
	dev_dbg(&port_client->dev, "set eth%d rx_los, read aoc type(0x%x) from reg(0x%02x) value.\n",
			 sda->index, ret, AOC_TYPE_REG);

	/* other value is error */
	if(ret != SFF8636_TYPE_VAL && ret != CMIS5P1_TYPE_VAL ) {
		dev_info(&port_client->dev, "this device is not recognized.\n");
		return -ENODEV;
	}

	type = (ret == SFF8636_TYPE_VAL ? SFF8636_TYPE : CMIS5P1_TYPE);
	page = (_info + type)->rx_los.page;
	reg = (_info + type)->rx_los.reg;
	dev_dbg(&port_client->dev, "get eth%d tx_fault, type=%d, page=0x%02x, reg=0x%02x.\n",
			 sda->index, type, page, reg);

	/* save the original page */
	_val = i2c_smbus_read_byte_data(port_client, SELECT_PAGE_REG);
	if(unlikely(_val < 0)) {
		dev_err(&port_client->dev, "set eth%d rx_los, read reg(0x%02x) failed _val=%d.\n",
				 sda->index, SELECT_PAGE_REG, _val);
		return _val;
	}
	dev_dbg(&port_client->dev, "set eth%d rx_los, read value(0x%x) from reg(0x%02x) value.\n",
			 sda->index, _val, SELECT_PAGE_REG);

	mutex_lock(&optoe_data->lock);
	ret = i2c_smbus_write_byte_data(port_client, SELECT_PAGE_REG, page);
	dev_dbg(&port_client->dev, "set eth%d rx_los, set value(0x%02x) into reg(0x%02x).\n",
			 sda->index, i2c_smbus_read_byte_data(port_client, SELECT_PAGE_REG),
			 SELECT_PAGE_REG);

	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "set eth%d rx_los failed, err code(%d).\n", sda->index, ret);
		mutex_unlock(&optoe_data->lock);
		return ret;
	}

	ret = i2c_smbus_read_byte_data(port_client, reg);
	if(unlikely(ret < 0)) {
		dev_err(&port_client->dev, "set eth%d rx_los failed, read reg(0x%02x) failed code(%d).\n",
				 sda->index, reg, ret);
		mutex_unlock(&optoe_data->lock);
		return ret;
	}

	i2c_smbus_write_byte_data(port_client, SELECT_PAGE_REG, _val);
	mutex_unlock(&optoe_data->lock);
	return snprintf(buf, PAGE_SIZE, "0x%02x\n", type == SFF8636_TYPE ? (ret & 0x0f) : ret);
}


static SENSOR_DEVICE_ATTR(sw_init_enable, 0644, init_enable_show, init_enable_store, 0);
static SENSOR_DEVICE_ATTR(all_port_power_on, 0644, all_port_power_on_show, all_port_power_on_store, 0);
static SENSOR_DEVICE_ATTR(id_led, 0644, id_led_show, id_led_store, 0);
static SENSOR_DEVICE_ATTR(id_led_mode, 0644, id_led_mode_show, id_led_mode_store, 0);
static SENSOR_DEVICE_ATTR(port_pwr_en_1, 0644, port_power_on_show, port_power_on_store, 0);
static SENSOR_DEVICE_ATTR(port_pwr_en_2, 0644, port_power_on_show, port_power_on_store, 1);
static SENSOR_DEVICE_ATTR(port_pwr_en_3, 0644, port_power_on_show, port_power_on_store, 2);
static SENSOR_DEVICE_ATTR(port_pwr_en_4, 0644, port_power_on_show, port_power_on_store, 3);
static SENSOR_DEVICE_ATTR(port_pwr_en_5, 0644, port_power_on_show, port_power_on_store, 4);
static SENSOR_DEVICE_ATTR(port1_rx_los, 0444, rx_los_show, NULL, 1);
static SENSOR_DEVICE_ATTR(port1_tx_fault, 0444, tx_fault_show, NULL, 1);
static SENSOR_DEVICE_ATTR(port1_tx_disable, 0644, tx_disable_show, tx_disable_store, 1);
static SENSOR_DEVICE_ATTR(port2_rx_los, 0444, rx_los_show, NULL, 2);
static SENSOR_DEVICE_ATTR(port2_tx_fault, 0444, tx_fault_show, NULL, 2);
static SENSOR_DEVICE_ATTR(port2_tx_disable, 0644, tx_disable_show, tx_disable_store, 2);
static SENSOR_DEVICE_ATTR(port3_rx_los, 0444, rx_los_show, NULL, 3);
static SENSOR_DEVICE_ATTR(port3_tx_fault, 0444, tx_fault_show, NULL, 3);
static SENSOR_DEVICE_ATTR(port3_tx_disable, 0644, tx_disable_show, tx_disable_store, 3);
static SENSOR_DEVICE_ATTR(port4_rx_los, 0444, rx_los_show, NULL, 4);
static SENSOR_DEVICE_ATTR(port4_tx_fault, 0444, tx_fault_show, NULL, 4);
static SENSOR_DEVICE_ATTR(port4_tx_disable, 0644, tx_disable_show, tx_disable_store, 4);
static SENSOR_DEVICE_ATTR(port5_rx_los, 0444, rx_los_show, NULL, 5);
static SENSOR_DEVICE_ATTR(port5_tx_fault, 0444, tx_fault_show, NULL, 5);
static SENSOR_DEVICE_ATTR(port5_tx_disable, 0644, tx_disable_show, tx_disable_store, 5);
static SENSOR_DEVICE_ATTR(port6_rx_los, 0444, rx_los_show, NULL, 6);
static SENSOR_DEVICE_ATTR(port6_tx_fault, 0444, tx_fault_show, NULL, 6);
static SENSOR_DEVICE_ATTR(port6_tx_disable, 0644, tx_disable_show, tx_disable_store, 6);
static SENSOR_DEVICE_ATTR(port7_rx_los, 0444, rx_los_show, NULL, 7);
static SENSOR_DEVICE_ATTR(port7_tx_fault, 0444, tx_fault_show, NULL, 7);
static SENSOR_DEVICE_ATTR(port7_tx_disable, 0644, tx_disable_show, tx_disable_store, 7);
static SENSOR_DEVICE_ATTR(port8_rx_los, 0444, rx_los_show, NULL, 8);
static SENSOR_DEVICE_ATTR(port8_tx_fault, 0444, tx_fault_show, NULL, 8);
static SENSOR_DEVICE_ATTR(port8_tx_disable, 0644, tx_disable_show, tx_disable_store, 8);

static SENSOR_DEVICE_ATTR(port9_rx_los, 0444, rx_los_show, NULL, 9);
static SENSOR_DEVICE_ATTR(port9_tx_fault, 0444, tx_fault_show, NULL, 9);
static SENSOR_DEVICE_ATTR(port9_tx_disable, 0644, tx_disable_show, tx_disable_store, 9);
static SENSOR_DEVICE_ATTR(port10_rx_los, 0444, rx_los_show, NULL, 10);
static SENSOR_DEVICE_ATTR(port10_tx_fault, 0444, tx_fault_show, NULL, 10);
static SENSOR_DEVICE_ATTR(port10_tx_disable, 0644, tx_disable_show, tx_disable_store, 10);
static SENSOR_DEVICE_ATTR(port11_rx_los, 0444, rx_los_show, NULL, 11);
static SENSOR_DEVICE_ATTR(port11_tx_fault, 0444, tx_fault_show, NULL, 11);
static SENSOR_DEVICE_ATTR(port11_tx_disable, 0644, tx_disable_show, tx_disable_store, 11);
static SENSOR_DEVICE_ATTR(port12_rx_los, 0444, rx_los_show, NULL, 12);
static SENSOR_DEVICE_ATTR(port12_tx_fault, 0444, tx_fault_show, NULL, 12);
static SENSOR_DEVICE_ATTR(port12_tx_disable, 0644, tx_disable_show, tx_disable_store, 12);
static SENSOR_DEVICE_ATTR(port13_rx_los, 0444, rx_los_show, NULL, 13);
static SENSOR_DEVICE_ATTR(port13_tx_fault, 0444, tx_fault_show, NULL,13);
static SENSOR_DEVICE_ATTR(port13_tx_disable, 0644, tx_disable_show, tx_disable_store, 13);
static SENSOR_DEVICE_ATTR(port14_rx_los, 0444, rx_los_show, NULL, 14);
static SENSOR_DEVICE_ATTR(port14_tx_fault, 0444, tx_fault_show, NULL, 14);
static SENSOR_DEVICE_ATTR(port14_tx_disable, 0644, tx_disable_show, tx_disable_store, 14);
static SENSOR_DEVICE_ATTR(port15_rx_los, 0444, rx_los_show, NULL, 15);
static SENSOR_DEVICE_ATTR(port15_tx_fault, 0444, tx_fault_show, NULL, 15);
static SENSOR_DEVICE_ATTR(port15_tx_disable, 0644, tx_disable_show, tx_disable_store, 15);
static SENSOR_DEVICE_ATTR(port16_rx_los, 0444, rx_los_show, NULL, 16);
static SENSOR_DEVICE_ATTR(port16_tx_fault, 0444, tx_fault_show, NULL, 16);
static SENSOR_DEVICE_ATTR(port16_tx_disable, 0644, tx_disable_show, tx_disable_store, 16);

static SENSOR_DEVICE_ATTR(port17_rx_los, 0444, rx_los_show, NULL, 17);
static SENSOR_DEVICE_ATTR(port17_tx_fault, 0444, tx_fault_show, NULL, 17);
static SENSOR_DEVICE_ATTR(port17_tx_disable, 0644, tx_disable_show, tx_disable_store, 17);
static SENSOR_DEVICE_ATTR(port18_rx_los, 0444, rx_los_show, NULL, 18);
static SENSOR_DEVICE_ATTR(port18_tx_fault, 0444, tx_fault_show, NULL, 18);
static SENSOR_DEVICE_ATTR(port18_tx_disable, 0644, tx_disable_show, tx_disable_store, 18);
static SENSOR_DEVICE_ATTR(port19_rx_los, 0444, rx_los_show, NULL, 19);
static SENSOR_DEVICE_ATTR(port19_tx_fault, 0444, tx_fault_show, NULL, 19);
static SENSOR_DEVICE_ATTR(port19_tx_disable, 0644, tx_disable_show, tx_disable_store, 19);
static SENSOR_DEVICE_ATTR(port20_rx_los, 0444, rx_los_show, NULL, 20);
static SENSOR_DEVICE_ATTR(port20_tx_fault, 0444, tx_fault_show, NULL, 20);
static SENSOR_DEVICE_ATTR(port20_tx_disable, 0644, tx_disable_show, tx_disable_store, 20);
static SENSOR_DEVICE_ATTR(port21_rx_los, 0444, rx_los_show, NULL, 21);
static SENSOR_DEVICE_ATTR(port21_tx_fault, 0444, tx_fault_show, NULL, 21);
static SENSOR_DEVICE_ATTR(port21_tx_disable, 0644, tx_disable_show, tx_disable_store, 21);
static SENSOR_DEVICE_ATTR(port22_rx_los, 0444, rx_los_show, NULL, 22);
static SENSOR_DEVICE_ATTR(port22_tx_fault, 0444, tx_fault_show, NULL, 22);
static SENSOR_DEVICE_ATTR(port22_tx_disable, 0644, tx_disable_show, tx_disable_store, 22);
static SENSOR_DEVICE_ATTR(port23_rx_los, 0444, rx_los_show, NULL, 23);
static SENSOR_DEVICE_ATTR(port23_tx_fault, 0444, tx_fault_show, NULL, 23);
static SENSOR_DEVICE_ATTR(port23_tx_disable, 0644, tx_disable_show, tx_disable_store, 23);
static SENSOR_DEVICE_ATTR(port24_rx_los, 0444, rx_los_show, NULL, 24);
static SENSOR_DEVICE_ATTR(port24_tx_fault, 0444, tx_fault_show, NULL, 24);
static SENSOR_DEVICE_ATTR(port24_tx_disable, 0644, tx_disable_show, tx_disable_store, 24);

static SENSOR_DEVICE_ATTR(port25_rx_los, 0444, rx_los_show, NULL, 25);
static SENSOR_DEVICE_ATTR(port25_tx_fault, 0444, tx_fault_show, NULL, 25);
static SENSOR_DEVICE_ATTR(port25_tx_disable, 0644, tx_disable_show, tx_disable_store, 25);
static SENSOR_DEVICE_ATTR(port26_rx_los, 0444, rx_los_show, NULL, 26);
static SENSOR_DEVICE_ATTR(port26_tx_fault, 0444, tx_fault_show, NULL, 26);
static SENSOR_DEVICE_ATTR(port26_tx_disable, 0644, tx_disable_show, tx_disable_store, 26);
static SENSOR_DEVICE_ATTR(port27_rx_los, 0444, rx_los_show, NULL, 27);
static SENSOR_DEVICE_ATTR(port27_tx_fault, 0444, tx_fault_show, NULL, 27);
static SENSOR_DEVICE_ATTR(port27_tx_disable, 0644, tx_disable_show, tx_disable_store, 27);
static SENSOR_DEVICE_ATTR(port28_rx_los, 0444, rx_los_show, NULL, 28);
static SENSOR_DEVICE_ATTR(port28_tx_fault, 0444, tx_fault_show, NULL, 28);
static SENSOR_DEVICE_ATTR(port28_tx_disable, 0644, tx_disable_show, tx_disable_store, 28);
static SENSOR_DEVICE_ATTR(port29_rx_los, 0444, rx_los_show, NULL, 29);
static SENSOR_DEVICE_ATTR(port29_tx_fault, 0444, tx_fault_show, NULL, 29);
static SENSOR_DEVICE_ATTR(port29_tx_disable, 0644, tx_disable_show, tx_disable_store, 29);
static SENSOR_DEVICE_ATTR(port30_rx_los, 0444, rx_los_show, NULL, 30);
static SENSOR_DEVICE_ATTR(port30_tx_fault, 0444, tx_fault_show, NULL, 30);
static SENSOR_DEVICE_ATTR(port30_tx_disable, 0644, tx_disable_show, tx_disable_store, 30);
static SENSOR_DEVICE_ATTR(port31_rx_los, 0444, rx_los_show, NULL, 31);
static SENSOR_DEVICE_ATTR(port31_tx_fault, 0444, tx_fault_show, NULL, 31);
static SENSOR_DEVICE_ATTR(port31_tx_disable, 0644, tx_disable_show, tx_disable_store, 31);
static SENSOR_DEVICE_ATTR(port32_rx_los, 0444, rx_los_show, NULL, 32);
static SENSOR_DEVICE_ATTR(port32_tx_fault, 0444, tx_fault_show, NULL, 32);
static SENSOR_DEVICE_ATTR(port32_tx_disable, 0644, tx_disable_show, tx_disable_store, 32);

static SENSOR_DEVICE_ATTR(port33_rx_los, 0444, rx_los_show, NULL, 33);
static SENSOR_DEVICE_ATTR(port33_tx_fault, 0444, tx_fault_show, NULL, 33);
static SENSOR_DEVICE_ATTR(port33_tx_disable, 0644, tx_disable_show, tx_disable_store, 33);
static SENSOR_DEVICE_ATTR(port34_rx_los, 0444, rx_los_show, NULL, 34);
static SENSOR_DEVICE_ATTR(port34_tx_fault, 0444, tx_fault_show, NULL, 34);
static SENSOR_DEVICE_ATTR(port34_tx_disable, 0644, tx_disable_show, tx_disable_store, 34);
static SENSOR_DEVICE_ATTR(port35_rx_los, 0444, rx_los_show, NULL, 35);
static SENSOR_DEVICE_ATTR(port35_tx_fault, 0444, tx_fault_show, NULL, 35);
static SENSOR_DEVICE_ATTR(port35_tx_disable, 0644, tx_disable_show, tx_disable_store, 35);
static SENSOR_DEVICE_ATTR(port36_rx_los, 0444, rx_los_show, NULL, 36);
static SENSOR_DEVICE_ATTR(port36_tx_fault, 0444, tx_fault_show, NULL, 36);
static SENSOR_DEVICE_ATTR(port36_tx_disable, 0644, tx_disable_show, tx_disable_store, 36);
static SENSOR_DEVICE_ATTR(port37_rx_los, 0444, rx_los_show, NULL, 37);
static SENSOR_DEVICE_ATTR(port37_tx_fault, 0444, tx_fault_show, NULL, 37);
static SENSOR_DEVICE_ATTR(port37_tx_disable, 0644, tx_disable_show, tx_disable_store, 37);
static SENSOR_DEVICE_ATTR(port38_rx_los, 0444, rx_los_show, NULL, 38);
static SENSOR_DEVICE_ATTR(port38_tx_fault, 0444, tx_fault_show, NULL, 38);
static SENSOR_DEVICE_ATTR(port38_tx_disable, 0644, tx_disable_show, tx_disable_store, 38);
static SENSOR_DEVICE_ATTR(port39_rx_los, 0444, rx_los_show, NULL, 39);
static SENSOR_DEVICE_ATTR(port39_tx_fault, 0444, tx_fault_show, NULL, 39);
static SENSOR_DEVICE_ATTR(port39_tx_disable, 0644, tx_disable_show, tx_disable_store, 39);
static SENSOR_DEVICE_ATTR(port40_rx_los, 0444, rx_los_show, NULL, 40);
static SENSOR_DEVICE_ATTR(port40_tx_fault, 0444, tx_fault_show, NULL, 40);
static SENSOR_DEVICE_ATTR(port40_tx_disable, 0644, tx_disable_show, tx_disable_store, 40);

static SENSOR_DEVICE_ATTR(port41_rx_los, 0444, rx_los_show, NULL, 41);
static SENSOR_DEVICE_ATTR(port41_tx_fault, 0444, tx_fault_show, NULL, 41);
static SENSOR_DEVICE_ATTR(port41_tx_disable, 0644, tx_disable_show, tx_disable_store, 41);
static SENSOR_DEVICE_ATTR(port42_rx_los, 0444, rx_los_show, NULL, 42);
static SENSOR_DEVICE_ATTR(port42_tx_fault, 0444, tx_fault_show, NULL, 42);
static SENSOR_DEVICE_ATTR(port42_tx_disable, 0644, tx_disable_show, tx_disable_store, 42);
static SENSOR_DEVICE_ATTR(port43_rx_los, 0444, rx_los_show, NULL, 43);
static SENSOR_DEVICE_ATTR(port43_tx_fault, 0444, tx_fault_show, NULL, 43);
static SENSOR_DEVICE_ATTR(port43_tx_disable, 0644, tx_disable_show, tx_disable_store, 43);
static SENSOR_DEVICE_ATTR(port44_rx_los, 0444, rx_los_show, NULL, 44);
static SENSOR_DEVICE_ATTR(port44_tx_fault, 0444, tx_fault_show, NULL, 44);
static SENSOR_DEVICE_ATTR(port44_tx_disable, 0644, tx_disable_show, tx_disable_store, 44);
static SENSOR_DEVICE_ATTR(port45_rx_los, 0444, rx_los_show, NULL, 45);
static SENSOR_DEVICE_ATTR(port45_tx_fault, 0444, tx_fault_show, NULL, 45);
static SENSOR_DEVICE_ATTR(port45_tx_disable, 0644, tx_disable_show, tx_disable_store, 45);
static SENSOR_DEVICE_ATTR(port46_rx_los, 0444, rx_los_show, NULL, 46);
static SENSOR_DEVICE_ATTR(port46_tx_fault, 0444, tx_fault_show, NULL, 46);
static SENSOR_DEVICE_ATTR(port46_tx_disable, 0644, tx_disable_show, tx_disable_store, 46);
static SENSOR_DEVICE_ATTR(port47_rx_los, 0444, rx_los_show, NULL, 47);
static SENSOR_DEVICE_ATTR(port47_tx_fault, 0444, tx_fault_show, NULL, 47);
static SENSOR_DEVICE_ATTR(port47_tx_disable, 0644, tx_disable_show, tx_disable_store, 47);
static SENSOR_DEVICE_ATTR(port48_rx_los, 0444, rx_los_show, NULL, 48);
static SENSOR_DEVICE_ATTR(port48_tx_fault, 0444, tx_fault_show, NULL, 48);
static SENSOR_DEVICE_ATTR(port48_tx_disable, 0644, tx_disable_show, tx_disable_store, 48);

static SENSOR_DEVICE_ATTR(port49_rx_los, 0444, rx_los_show, NULL, 49);
static SENSOR_DEVICE_ATTR(port49_tx_fault, 0444, tx_fault_show, NULL, 49);
static SENSOR_DEVICE_ATTR(port49_tx_disable, 0644, tx_disable_show, tx_disable_store, 49);
static SENSOR_DEVICE_ATTR(port50_rx_los, 0444, rx_los_show, NULL, 50);
static SENSOR_DEVICE_ATTR(port50_tx_fault, 0444, tx_fault_show, NULL, 50);
static SENSOR_DEVICE_ATTR(port50_tx_disable, 0644, tx_disable_show, tx_disable_store, 50);
static SENSOR_DEVICE_ATTR(port51_rx_los, 0444, rx_los_show, NULL, 51);
static SENSOR_DEVICE_ATTR(port51_tx_fault, 0444, tx_fault_show, NULL, 51);
static SENSOR_DEVICE_ATTR(port51_tx_disable, 0644, tx_disable_show, tx_disable_store, 51);
static SENSOR_DEVICE_ATTR(port52_rx_los, 0444, rx_los_show, NULL, 52);
static SENSOR_DEVICE_ATTR(port52_tx_fault, 0444, tx_fault_show, NULL, 52);
static SENSOR_DEVICE_ATTR(port52_tx_disable, 0644, tx_disable_show, tx_disable_store, 52);
static SENSOR_DEVICE_ATTR(port53_rx_los, 0444, rx_los_show, NULL, 53);
static SENSOR_DEVICE_ATTR(port53_tx_fault, 0444, tx_fault_show, NULL, 53);
static SENSOR_DEVICE_ATTR(port53_tx_disable, 0644, tx_disable_show, tx_disable_store, 53);
static SENSOR_DEVICE_ATTR(port54_rx_los, 0444, rx_los_show, NULL, 54);
static SENSOR_DEVICE_ATTR(port54_tx_fault, 0444, tx_fault_show, NULL, 54);
static SENSOR_DEVICE_ATTR(port54_tx_disable, 0644, tx_disable_show, tx_disable_store, 54);
static SENSOR_DEVICE_ATTR(port55_rx_los, 0444, rx_los_show, NULL, 55);
static SENSOR_DEVICE_ATTR(port55_tx_fault, 0444, tx_fault_show, NULL, 55);
static SENSOR_DEVICE_ATTR(port55_tx_disable, 0644, tx_disable_show, tx_disable_store, 55);
static SENSOR_DEVICE_ATTR(port56_rx_los, 0444, rx_los_show, NULL, 56);
static SENSOR_DEVICE_ATTR(port56_tx_fault, 0444, tx_fault_show, NULL, 56);
static SENSOR_DEVICE_ATTR(port56_tx_disable, 0644, tx_disable_show, tx_disable_store, 56);

static SENSOR_DEVICE_ATTR(port57_rx_los, 0444, rx_los_show, NULL, 57);
static SENSOR_DEVICE_ATTR(port57_tx_fault, 0444, tx_fault_show, NULL, 57);
static SENSOR_DEVICE_ATTR(port57_tx_disable, 0644, tx_disable_show, tx_disable_store, 57);
static SENSOR_DEVICE_ATTR(port58_rx_los, 0444, rx_los_show, NULL, 58);
static SENSOR_DEVICE_ATTR(port58_tx_fault, 0444, tx_fault_show, NULL, 58);
static SENSOR_DEVICE_ATTR(port58_tx_disable, 0644, tx_disable_show, tx_disable_store, 58);
static SENSOR_DEVICE_ATTR(port59_rx_los, 0444, rx_los_show, NULL, 59);
static SENSOR_DEVICE_ATTR(port59_tx_fault, 0444, tx_fault_show, NULL, 59);
static SENSOR_DEVICE_ATTR(port59_tx_disable, 0644, tx_disable_show, tx_disable_store, 59);
static SENSOR_DEVICE_ATTR(port60_rx_los, 0444, rx_los_show, NULL, 60);
static SENSOR_DEVICE_ATTR(port60_tx_fault, 0444, tx_fault_show, NULL, 60);
static SENSOR_DEVICE_ATTR(port60_tx_disable, 0644, tx_disable_show, tx_disable_store, 60);
static SENSOR_DEVICE_ATTR(port61_rx_los, 0444, rx_los_show, NULL, 61);
static SENSOR_DEVICE_ATTR(port61_tx_fault, 0444, tx_fault_show, NULL, 61);
static SENSOR_DEVICE_ATTR(port61_tx_disable, 0644, tx_disable_show, tx_disable_store, 61);
static SENSOR_DEVICE_ATTR(port62_rx_los, 0444, rx_los_show, NULL, 62);
static SENSOR_DEVICE_ATTR(port62_tx_fault, 0444, tx_fault_show, NULL, 62);
static SENSOR_DEVICE_ATTR(port62_tx_disable, 0644, tx_disable_show, tx_disable_store, 62);
static SENSOR_DEVICE_ATTR(port63_rx_los, 0444, rx_los_show, NULL, 63);
static SENSOR_DEVICE_ATTR(port63_tx_fault, 0444, tx_fault_show, NULL, 63);
static SENSOR_DEVICE_ATTR(port63_tx_disable, 0644, tx_disable_show, tx_disable_store, 63);
static SENSOR_DEVICE_ATTR(port64_rx_los, 0444, rx_los_show, NULL, 64);
static SENSOR_DEVICE_ATTR(port64_tx_fault, 0444, tx_fault_show, NULL, 64);
static SENSOR_DEVICE_ATTR(port64_tx_disable, 0644, tx_disable_show, tx_disable_store, 64);

static SENSOR_DEVICE_ATTR(port65_rx_los, 0444, rx_los_show, NULL, 65);
static SENSOR_DEVICE_ATTR(port65_tx_fault, 0444, tx_fault_show, NULL, 65);
static SENSOR_DEVICE_ATTR(port65_tx_disable, 0644, tx_disable_show, tx_disable_store, 65);
static SENSOR_DEVICE_ATTR(port66_rx_los, 0444, rx_los_show, NULL, 66);
static SENSOR_DEVICE_ATTR(port66_tx_fault, 0444, tx_fault_show, NULL, 66);
static SENSOR_DEVICE_ATTR(port66_tx_disable, 0644, tx_disable_show, tx_disable_store, 66);
static SENSOR_DEVICE_ATTR(port67_rx_los, 0444, rx_los_show, NULL, 67);
static SENSOR_DEVICE_ATTR(port67_tx_fault, 0444, tx_fault_show, NULL, 67);
static SENSOR_DEVICE_ATTR(port67_tx_disable, 0644, tx_disable_show, tx_disable_store, 67);
static SENSOR_DEVICE_ATTR(port68_rx_los, 0444, rx_los_show, NULL, 68);
static SENSOR_DEVICE_ATTR(port68_tx_fault, 0444, tx_fault_show, NULL, 68);
static SENSOR_DEVICE_ATTR(port68_tx_disable, 0644, tx_disable_show, tx_disable_store, 68);
static SENSOR_DEVICE_ATTR(port69_rx_los, 0444, rx_los_show, NULL, 69);
static SENSOR_DEVICE_ATTR(port69_tx_fault, 0444, tx_fault_show, NULL, 69);
static SENSOR_DEVICE_ATTR(port69_tx_disable, 0644, tx_disable_show, tx_disable_store, 69);
static SENSOR_DEVICE_ATTR(port70_rx_los, 0444, rx_los_show, NULL, 70);
static SENSOR_DEVICE_ATTR(port70_tx_fault, 0444, tx_fault_show, NULL, 70);
static SENSOR_DEVICE_ATTR(port70_tx_disable, 0644, tx_disable_show, tx_disable_store, 70);
static SENSOR_DEVICE_ATTR(port71_rx_los, 0444, rx_los_show, NULL, 71);
static SENSOR_DEVICE_ATTR(port71_tx_fault, 0444, tx_fault_show, NULL, 71);
static SENSOR_DEVICE_ATTR(port71_tx_disable, 0644, tx_disable_show, tx_disable_store, 71);
static SENSOR_DEVICE_ATTR(port72_rx_los, 0444, rx_los_show, NULL, 72);
static SENSOR_DEVICE_ATTR(port72_tx_fault, 0444, tx_fault_show, NULL, 72);
static SENSOR_DEVICE_ATTR(port72_tx_disable, 0644, tx_disable_show, tx_disable_store, 72);

static SENSOR_DEVICE_ATTR(port73_rx_los, 0444, rx_los_show, NULL, 73);
static SENSOR_DEVICE_ATTR(port73_tx_fault, 0444, tx_fault_show, NULL, 73);
static SENSOR_DEVICE_ATTR(port73_tx_disable, 0644, tx_disable_show, tx_disable_store, 73);
static SENSOR_DEVICE_ATTR(port74_rx_los, 0444, rx_los_show, NULL, 74);
static SENSOR_DEVICE_ATTR(port74_tx_fault, 0444, tx_fault_show, NULL, 74);
static SENSOR_DEVICE_ATTR(port74_tx_disable, 0644, tx_disable_show, tx_disable_store, 74);
static SENSOR_DEVICE_ATTR(port75_rx_los, 0444, rx_los_show, NULL, 75);
static SENSOR_DEVICE_ATTR(port75_tx_fault, 0444, tx_fault_show, NULL, 75);
static SENSOR_DEVICE_ATTR(port75_tx_disable, 0644, tx_disable_show, tx_disable_store, 75);
static SENSOR_DEVICE_ATTR(port76_rx_los, 0444, rx_los_show, NULL, 76);
static SENSOR_DEVICE_ATTR(port76_tx_fault, 0444, tx_fault_show, NULL, 76);
static SENSOR_DEVICE_ATTR(port76_tx_disable, 0644, tx_disable_show, tx_disable_store, 76);
static SENSOR_DEVICE_ATTR(port77_rx_los, 0444, rx_los_show, NULL, 77);
static SENSOR_DEVICE_ATTR(port77_tx_fault, 0444, tx_fault_show, NULL, 77);
static SENSOR_DEVICE_ATTR(port77_tx_disable, 0644, tx_disable_show, tx_disable_store, 77);
static SENSOR_DEVICE_ATTR(port78_rx_los, 0444, rx_los_show, NULL, 78);
static SENSOR_DEVICE_ATTR(port78_tx_fault, 0444, tx_fault_show, NULL, 78);
static SENSOR_DEVICE_ATTR(port78_tx_disable, 0644, tx_disable_show, tx_disable_store, 78);
static SENSOR_DEVICE_ATTR(port79_rx_los, 0444, rx_los_show, NULL, 79);
static SENSOR_DEVICE_ATTR(port79_tx_fault, 0444, tx_fault_show, NULL, 79);
static SENSOR_DEVICE_ATTR(port79_tx_disable, 0644, tx_disable_show, tx_disable_store, 79);
static SENSOR_DEVICE_ATTR(port80_rx_los, 0444, rx_los_show, NULL, 80);
static SENSOR_DEVICE_ATTR(port80_tx_fault, 0444, tx_fault_show, NULL, 80);
static SENSOR_DEVICE_ATTR(port80_tx_disable, 0644, tx_disable_show, tx_disable_store, 80);

static SENSOR_DEVICE_ATTR(port81_rx_los, 0444, rx_los_show, NULL, 81);
static SENSOR_DEVICE_ATTR(port81_tx_fault, 0444, tx_fault_show, NULL, 81);
static SENSOR_DEVICE_ATTR(port81_tx_disable, 0644, tx_disable_show, tx_disable_store, 81);
static SENSOR_DEVICE_ATTR(port82_rx_los, 0444, rx_los_show, NULL, 82);
static SENSOR_DEVICE_ATTR(port82_tx_fault, 0444, tx_fault_show, NULL, 82);
static SENSOR_DEVICE_ATTR(port82_tx_disable, 0644, tx_disable_show, tx_disable_store, 82);
static SENSOR_DEVICE_ATTR(port83_rx_los, 0444, rx_los_show, NULL, 83);
static SENSOR_DEVICE_ATTR(port83_tx_fault, 0444, tx_fault_show, NULL, 83);
static SENSOR_DEVICE_ATTR(port83_tx_disable, 0644, tx_disable_show, tx_disable_store, 83);
static SENSOR_DEVICE_ATTR(port84_rx_los, 0444, rx_los_show, NULL, 84);
static SENSOR_DEVICE_ATTR(port84_tx_fault, 0444, tx_fault_show, NULL, 84);
static SENSOR_DEVICE_ATTR(port84_tx_disable, 0644, tx_disable_show, tx_disable_store, 84);
static SENSOR_DEVICE_ATTR(port85_rx_los, 0444, rx_los_show, NULL, 85);
static SENSOR_DEVICE_ATTR(port85_tx_fault, 0444, tx_fault_show, NULL, 85);
static SENSOR_DEVICE_ATTR(port85_tx_disable, 0644, tx_disable_show, tx_disable_store, 85);
static SENSOR_DEVICE_ATTR(port86_rx_los, 0444, rx_los_show, NULL, 86);
static SENSOR_DEVICE_ATTR(port86_tx_fault, 0444, tx_fault_show, NULL, 86);
static SENSOR_DEVICE_ATTR(port86_tx_disable, 0644, tx_disable_show, tx_disable_store, 86);
static SENSOR_DEVICE_ATTR(port87_rx_los, 0444, rx_los_show, NULL, 87);
static SENSOR_DEVICE_ATTR(port87_tx_fault, 0444, tx_fault_show, NULL, 87);
static SENSOR_DEVICE_ATTR(port87_tx_disable, 0644, tx_disable_show, tx_disable_store, 87);
static SENSOR_DEVICE_ATTR(port88_rx_los, 0444, rx_los_show, NULL, 88);
static SENSOR_DEVICE_ATTR(port88_tx_fault, 0444, tx_fault_show, NULL, 88);
static SENSOR_DEVICE_ATTR(port88_tx_disable, 0644, tx_disable_show, tx_disable_store, 88);

static SENSOR_DEVICE_ATTR(port89_rx_los, 0444, rx_los_show, NULL, 89);
static SENSOR_DEVICE_ATTR(port89_tx_fault, 0444, tx_fault_show, NULL, 89);
static SENSOR_DEVICE_ATTR(port89_tx_disable, 0644, tx_disable_show, tx_disable_store, 89);
static SENSOR_DEVICE_ATTR(port90_rx_los, 0444, rx_los_show, NULL, 90);
static SENSOR_DEVICE_ATTR(port90_tx_fault, 0444, tx_fault_show, NULL, 90);
static SENSOR_DEVICE_ATTR(port90_tx_disable, 0644, tx_disable_show, tx_disable_store, 90);
static SENSOR_DEVICE_ATTR(port91_rx_los, 0444, rx_los_show, NULL, 91);
static SENSOR_DEVICE_ATTR(port91_tx_fault, 0444, tx_fault_show, NULL, 91);
static SENSOR_DEVICE_ATTR(port91_tx_disable, 0644, tx_disable_show, tx_disable_store, 91);
static SENSOR_DEVICE_ATTR(port92_rx_los, 0444, rx_los_show, NULL, 92);
static SENSOR_DEVICE_ATTR(port92_tx_fault, 0444, tx_fault_show, NULL, 92);
static SENSOR_DEVICE_ATTR(port92_tx_disable, 0644, tx_disable_show, tx_disable_store, 92);
static SENSOR_DEVICE_ATTR(port93_rx_los, 0444, rx_los_show, NULL, 93);
static SENSOR_DEVICE_ATTR(port93_tx_fault, 0444, tx_fault_show, NULL, 93);
static SENSOR_DEVICE_ATTR(port93_tx_disable, 0644, tx_disable_show, tx_disable_store, 93);
static SENSOR_DEVICE_ATTR(port94_rx_los, 0444, rx_los_show, NULL, 94);
static SENSOR_DEVICE_ATTR(port94_tx_fault, 0444, tx_fault_show, NULL, 94);
static SENSOR_DEVICE_ATTR(port94_tx_disable, 0644, tx_disable_show, tx_disable_store, 94);
static SENSOR_DEVICE_ATTR(port95_rx_los, 0444, rx_los_show, NULL, 95);
static SENSOR_DEVICE_ATTR(port95_tx_fault, 0444, tx_fault_show, NULL, 95);
static SENSOR_DEVICE_ATTR(port95_tx_disable, 0644, tx_disable_show, tx_disable_store, 95);
static SENSOR_DEVICE_ATTR(port96_rx_los, 0444, rx_los_show, NULL, 96);
static SENSOR_DEVICE_ATTR(port96_tx_fault, 0444, tx_fault_show, NULL, 96);
static SENSOR_DEVICE_ATTR(port96_tx_disable, 0644, tx_disable_show, tx_disable_store, 96);

static SENSOR_DEVICE_ATTR(port97_rx_los, 0444, rx_los_show, NULL, 97);
static SENSOR_DEVICE_ATTR(port97_tx_fault, 0444, tx_fault_show, NULL, 97);
static SENSOR_DEVICE_ATTR(port97_tx_disable, 0644, tx_disable_show, tx_disable_store, 97);
static SENSOR_DEVICE_ATTR(port98_rx_los, 0444, rx_los_show, NULL, 98);
static SENSOR_DEVICE_ATTR(port98_tx_fault, 0444, tx_fault_show, NULL, 98);
static SENSOR_DEVICE_ATTR(port98_tx_disable, 0644, tx_disable_show, tx_disable_store, 98);
static SENSOR_DEVICE_ATTR(port99_rx_los, 0444, rx_los_show, NULL, 99);
static SENSOR_DEVICE_ATTR(port99_tx_fault, 0444, tx_fault_show, NULL, 99);
static SENSOR_DEVICE_ATTR(port99_tx_disable, 0644, tx_disable_show, tx_disable_store, 99);
static SENSOR_DEVICE_ATTR(port100_rx_los, 0444, rx_los_show, NULL, 100);
static SENSOR_DEVICE_ATTR(port100_tx_fault, 0444, tx_fault_show, NULL, 100);
static SENSOR_DEVICE_ATTR(port100_tx_disable, 0644, tx_disable_show, tx_disable_store, 100);
static SENSOR_DEVICE_ATTR(port101_rx_los, 0444, rx_los_show, NULL, 101);
static SENSOR_DEVICE_ATTR(port101_tx_fault, 0444, tx_fault_show, NULL, 101);
static SENSOR_DEVICE_ATTR(port101_tx_disable, 0644, tx_disable_show, tx_disable_store, 101);
static SENSOR_DEVICE_ATTR(port102_rx_los, 0444, rx_los_show, NULL, 102);
static SENSOR_DEVICE_ATTR(port102_tx_fault, 0444, tx_fault_show, NULL, 102);
static SENSOR_DEVICE_ATTR(port102_tx_disable, 0644, tx_disable_show, tx_disable_store, 102);
static SENSOR_DEVICE_ATTR(port103_rx_los, 0444, rx_los_show, NULL, 103);
static SENSOR_DEVICE_ATTR(port103_tx_fault, 0444, tx_fault_show, NULL, 103);
static SENSOR_DEVICE_ATTR(port103_tx_disable, 0644, tx_disable_show, tx_disable_store, 103);
static SENSOR_DEVICE_ATTR(port104_rx_los, 0444, rx_los_show, NULL, 104);
static SENSOR_DEVICE_ATTR(port104_tx_fault, 0444, tx_fault_show, NULL, 104);
static SENSOR_DEVICE_ATTR(port104_tx_disable, 0644, tx_disable_show, tx_disable_store, 104);

static SENSOR_DEVICE_ATTR(port105_rx_los, 0444, rx_los_show, NULL, 105);
static SENSOR_DEVICE_ATTR(port105_tx_fault, 0444, tx_fault_show, NULL, 105);
static SENSOR_DEVICE_ATTR(port105_tx_disable, 0644, tx_disable_show, tx_disable_store, 105);
static SENSOR_DEVICE_ATTR(port106_rx_los, 0444, rx_los_show, NULL, 106);
static SENSOR_DEVICE_ATTR(port106_tx_fault, 0444, tx_fault_show, NULL, 106);
static SENSOR_DEVICE_ATTR(port106_tx_disable, 0644, tx_disable_show, tx_disable_store, 106);
static SENSOR_DEVICE_ATTR(port107_rx_los, 0444, rx_los_show, NULL, 107);
static SENSOR_DEVICE_ATTR(port107_tx_fault, 0444, tx_fault_show, NULL, 107);
static SENSOR_DEVICE_ATTR(port107_tx_disable, 0644, tx_disable_show, tx_disable_store, 107);
static SENSOR_DEVICE_ATTR(port108_rx_los, 0444, rx_los_show, NULL, 108);
static SENSOR_DEVICE_ATTR(port108_tx_fault, 0444, tx_fault_show, NULL, 108);
static SENSOR_DEVICE_ATTR(port108_tx_disable, 0644, tx_disable_show, tx_disable_store, 108);
static SENSOR_DEVICE_ATTR(port109_rx_los, 0444, rx_los_show, NULL, 109);
static SENSOR_DEVICE_ATTR(port109_tx_fault, 0444, tx_fault_show, NULL, 109);
static SENSOR_DEVICE_ATTR(port109_tx_disable, 0644, tx_disable_show, tx_disable_store, 109);
static SENSOR_DEVICE_ATTR(port110_rx_los, 0444, rx_los_show, NULL, 110);
static SENSOR_DEVICE_ATTR(port110_tx_fault, 0444, tx_fault_show, NULL, 110);
static SENSOR_DEVICE_ATTR(port110_tx_disable, 0644, tx_disable_show, tx_disable_store, 110);
static SENSOR_DEVICE_ATTR(port111_rx_los, 0444, rx_los_show, NULL, 111);
static SENSOR_DEVICE_ATTR(port111_tx_fault, 0444, tx_fault_show, NULL, 111);
static SENSOR_DEVICE_ATTR(port111_tx_disable, 0644, tx_disable_show, tx_disable_store, 111);
static SENSOR_DEVICE_ATTR(port112_rx_los, 0444, rx_los_show, NULL, 112);
static SENSOR_DEVICE_ATTR(port112_tx_fault, 0444, tx_fault_show, NULL, 112);
static SENSOR_DEVICE_ATTR(port112_tx_disable, 0644, tx_disable_show, tx_disable_store, 112);

static SENSOR_DEVICE_ATTR(port113_rx_los, 0444, rx_los_show, NULL, 113);
static SENSOR_DEVICE_ATTR(port113_tx_fault, 0444, tx_fault_show, NULL, 113);
static SENSOR_DEVICE_ATTR(port113_tx_disable, 0644, tx_disable_show, tx_disable_store, 113);
static SENSOR_DEVICE_ATTR(port114_rx_los, 0444, rx_los_show, NULL, 114);
static SENSOR_DEVICE_ATTR(port114_tx_fault, 0444, tx_fault_show, NULL, 114);
static SENSOR_DEVICE_ATTR(port114_tx_disable, 0644, tx_disable_show, tx_disable_store, 114);
static SENSOR_DEVICE_ATTR(port115_rx_los, 0444, rx_los_show, NULL, 115);
static SENSOR_DEVICE_ATTR(port115_tx_fault, 0444, tx_fault_show, NULL, 115);
static SENSOR_DEVICE_ATTR(port115_tx_disable, 0644, tx_disable_show, tx_disable_store, 115);
static SENSOR_DEVICE_ATTR(port116_rx_los, 0444, rx_los_show, NULL, 116);
static SENSOR_DEVICE_ATTR(port116_tx_fault, 0444, tx_fault_show, NULL, 116);
static SENSOR_DEVICE_ATTR(port116_tx_disable, 0644, tx_disable_show, tx_disable_store, 116);
static SENSOR_DEVICE_ATTR(port117_rx_los, 0444, rx_los_show, NULL, 117);
static SENSOR_DEVICE_ATTR(port117_tx_fault, 0444, tx_fault_show, NULL, 117);
static SENSOR_DEVICE_ATTR(port117_tx_disable, 0644, tx_disable_show, tx_disable_store, 117);
static SENSOR_DEVICE_ATTR(port118_rx_los, 0444, rx_los_show, NULL, 118);
static SENSOR_DEVICE_ATTR(port118_tx_fault, 0444, tx_fault_show, NULL, 118);
static SENSOR_DEVICE_ATTR(port118_tx_disable, 0644, tx_disable_show, tx_disable_store, 118);
static SENSOR_DEVICE_ATTR(port119_rx_los, 0444, rx_los_show, NULL, 119);
static SENSOR_DEVICE_ATTR(port119_tx_fault, 0444, tx_fault_show, NULL, 119);
static SENSOR_DEVICE_ATTR(port119_tx_disable, 0644, tx_disable_show, tx_disable_store, 119);
static SENSOR_DEVICE_ATTR(port120_rx_los, 0444, rx_los_show, NULL, 120);
static SENSOR_DEVICE_ATTR(port120_tx_fault, 0444, tx_fault_show, NULL, 120);
static SENSOR_DEVICE_ATTR(port120_tx_disable, 0644, tx_disable_show, tx_disable_store, 120);

static SENSOR_DEVICE_ATTR(port121_rx_los, 0444, rx_los_show, NULL, 121);
static SENSOR_DEVICE_ATTR(port121_tx_fault, 0444, tx_fault_show, NULL, 121);
static SENSOR_DEVICE_ATTR(port121_tx_disable, 0644, tx_disable_show, tx_disable_store, 121);
static SENSOR_DEVICE_ATTR(port122_rx_los, 0444, rx_los_show, NULL, 122);
static SENSOR_DEVICE_ATTR(port122_tx_fault, 0444, tx_fault_show, NULL, 122);
static SENSOR_DEVICE_ATTR(port122_tx_disable, 0644, tx_disable_show, tx_disable_store, 122);
static SENSOR_DEVICE_ATTR(port123_rx_los, 0444, rx_los_show, NULL, 123);
static SENSOR_DEVICE_ATTR(port123_tx_fault, 0444, tx_fault_show, NULL, 123);
static SENSOR_DEVICE_ATTR(port123_tx_disable, 0644, tx_disable_show, tx_disable_store, 123);
static SENSOR_DEVICE_ATTR(port124_rx_los, 0444, rx_los_show, NULL, 124);
static SENSOR_DEVICE_ATTR(port124_tx_fault, 0444, tx_fault_show, NULL, 124);
static SENSOR_DEVICE_ATTR(port124_tx_disable, 0644, tx_disable_show, tx_disable_store, 124);
static SENSOR_DEVICE_ATTR(port125_rx_los, 0444, rx_los_show, NULL, 125);
static SENSOR_DEVICE_ATTR(port125_tx_fault, 0444, tx_fault_show, NULL, 125);
static SENSOR_DEVICE_ATTR(port125_tx_disable, 0644, tx_disable_show, tx_disable_store, 125);
static SENSOR_DEVICE_ATTR(port126_rx_los, 0444, rx_los_show, NULL, 126);
static SENSOR_DEVICE_ATTR(port126_tx_fault, 0444, tx_fault_show, NULL, 126);
static SENSOR_DEVICE_ATTR(port126_tx_disable, 0644, tx_disable_show, tx_disable_store, 126);
static SENSOR_DEVICE_ATTR(port127_rx_los, 0444, rx_los_show, NULL, 127);
static SENSOR_DEVICE_ATTR(port127_tx_fault, 0444, tx_fault_show, NULL, 127);
static SENSOR_DEVICE_ATTR(port127_tx_disable, 0644, tx_disable_show, tx_disable_store, 127);
static SENSOR_DEVICE_ATTR(port128_rx_los, 0444, rx_los_show, NULL, 128);
static SENSOR_DEVICE_ATTR(port128_tx_fault, 0444, tx_fault_show, NULL, 128);
static SENSOR_DEVICE_ATTR(port128_tx_disable, 0644, tx_disable_show, tx_disable_store, 128);

static struct attribute *port_cpld_attributes[] = {
	&sensor_dev_attr_sw_init_enable.dev_attr.attr,
	&sensor_dev_attr_all_port_power_on.dev_attr.attr,
	&sensor_dev_attr_id_led.dev_attr.attr,
	&sensor_dev_attr_id_led_mode.dev_attr.attr,
	&sensor_dev_attr_port_pwr_en_1.dev_attr.attr,
	&sensor_dev_attr_port_pwr_en_2.dev_attr.attr,
	&sensor_dev_attr_port_pwr_en_3.dev_attr.attr,
	&sensor_dev_attr_port_pwr_en_4.dev_attr.attr,
	&sensor_dev_attr_port_pwr_en_5.dev_attr.attr,

	&sensor_dev_attr_port1_rx_los.dev_attr.attr,
	&sensor_dev_attr_port1_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port1_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port2_rx_los.dev_attr.attr,
	&sensor_dev_attr_port2_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port2_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port3_rx_los.dev_attr.attr,
	&sensor_dev_attr_port3_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port3_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port4_rx_los.dev_attr.attr,
	&sensor_dev_attr_port4_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port4_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port5_rx_los.dev_attr.attr,
	&sensor_dev_attr_port5_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port5_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port6_rx_los.dev_attr.attr,
	&sensor_dev_attr_port6_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port6_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port7_rx_los.dev_attr.attr,
	&sensor_dev_attr_port7_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port7_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port8_rx_los.dev_attr.attr,
	&sensor_dev_attr_port8_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port8_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port9_rx_los.dev_attr.attr,
	&sensor_dev_attr_port9_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port9_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port10_rx_los.dev_attr.attr,
	&sensor_dev_attr_port10_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port10_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port11_rx_los.dev_attr.attr,
	&sensor_dev_attr_port11_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port11_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port12_rx_los.dev_attr.attr,
	&sensor_dev_attr_port12_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port12_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port13_rx_los.dev_attr.attr,
	&sensor_dev_attr_port13_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port13_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port14_rx_los.dev_attr.attr,
	&sensor_dev_attr_port14_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port14_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port15_rx_los.dev_attr.attr,
	&sensor_dev_attr_port15_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port15_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port16_rx_los.dev_attr.attr,
	&sensor_dev_attr_port16_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port16_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port17_rx_los.dev_attr.attr,
	&sensor_dev_attr_port17_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port17_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port18_rx_los.dev_attr.attr,
	&sensor_dev_attr_port18_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port18_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port19_rx_los.dev_attr.attr,
	&sensor_dev_attr_port19_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port19_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port20_rx_los.dev_attr.attr,
	&sensor_dev_attr_port20_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port20_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port21_rx_los.dev_attr.attr,
	&sensor_dev_attr_port21_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port21_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port22_rx_los.dev_attr.attr,
	&sensor_dev_attr_port22_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port22_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port23_rx_los.dev_attr.attr,
	&sensor_dev_attr_port23_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port23_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port24_rx_los.dev_attr.attr,
	&sensor_dev_attr_port24_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port24_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port25_rx_los.dev_attr.attr,
	&sensor_dev_attr_port25_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port25_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port26_rx_los.dev_attr.attr,
	&sensor_dev_attr_port26_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port26_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port27_rx_los.dev_attr.attr,
	&sensor_dev_attr_port27_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port27_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port28_rx_los.dev_attr.attr,
	&sensor_dev_attr_port28_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port28_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port29_rx_los.dev_attr.attr,
	&sensor_dev_attr_port29_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port29_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port30_rx_los.dev_attr.attr,
	&sensor_dev_attr_port30_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port30_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port31_rx_los.dev_attr.attr,
	&sensor_dev_attr_port31_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port31_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port32_rx_los.dev_attr.attr,
	&sensor_dev_attr_port32_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port32_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port33_rx_los.dev_attr.attr,
	&sensor_dev_attr_port33_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port33_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port34_rx_los.dev_attr.attr,
	&sensor_dev_attr_port34_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port34_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port35_rx_los.dev_attr.attr,
	&sensor_dev_attr_port35_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port35_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port36_rx_los.dev_attr.attr,
	&sensor_dev_attr_port36_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port36_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port37_rx_los.dev_attr.attr,
	&sensor_dev_attr_port37_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port37_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port38_rx_los.dev_attr.attr,
	&sensor_dev_attr_port38_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port38_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port39_rx_los.dev_attr.attr,
	&sensor_dev_attr_port39_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port39_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port40_rx_los.dev_attr.attr,
	&sensor_dev_attr_port40_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port40_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port41_rx_los.dev_attr.attr,
	&sensor_dev_attr_port41_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port41_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port42_rx_los.dev_attr.attr,
	&sensor_dev_attr_port42_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port42_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port43_rx_los.dev_attr.attr,
	&sensor_dev_attr_port43_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port43_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port44_rx_los.dev_attr.attr,
	&sensor_dev_attr_port44_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port44_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port45_rx_los.dev_attr.attr,
	&sensor_dev_attr_port45_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port45_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port46_rx_los.dev_attr.attr,
	&sensor_dev_attr_port46_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port46_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port47_rx_los.dev_attr.attr,
	&sensor_dev_attr_port47_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port47_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port48_rx_los.dev_attr.attr,
	&sensor_dev_attr_port48_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port48_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port49_rx_los.dev_attr.attr,
	&sensor_dev_attr_port49_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port49_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port50_rx_los.dev_attr.attr,
	&sensor_dev_attr_port50_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port50_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port51_rx_los.dev_attr.attr,
	&sensor_dev_attr_port51_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port51_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port52_rx_los.dev_attr.attr,
	&sensor_dev_attr_port52_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port52_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port53_rx_los.dev_attr.attr,
	&sensor_dev_attr_port53_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port53_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port54_rx_los.dev_attr.attr,
	&sensor_dev_attr_port54_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port54_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port55_rx_los.dev_attr.attr,
	&sensor_dev_attr_port55_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port55_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port56_rx_los.dev_attr.attr,
	&sensor_dev_attr_port56_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port56_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port57_rx_los.dev_attr.attr,
	&sensor_dev_attr_port57_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port57_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port58_rx_los.dev_attr.attr,
	&sensor_dev_attr_port58_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port58_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port59_rx_los.dev_attr.attr,
	&sensor_dev_attr_port59_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port59_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port60_rx_los.dev_attr.attr,
	&sensor_dev_attr_port60_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port60_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port61_rx_los.dev_attr.attr,
	&sensor_dev_attr_port61_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port61_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port62_rx_los.dev_attr.attr,
	&sensor_dev_attr_port62_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port62_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port63_rx_los.dev_attr.attr,
	&sensor_dev_attr_port63_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port63_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port64_rx_los.dev_attr.attr,
	&sensor_dev_attr_port64_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port64_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port65_rx_los.dev_attr.attr,
	&sensor_dev_attr_port65_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port65_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port66_rx_los.dev_attr.attr,
	&sensor_dev_attr_port66_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port66_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port67_rx_los.dev_attr.attr,
	&sensor_dev_attr_port67_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port67_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port68_rx_los.dev_attr.attr,
	&sensor_dev_attr_port68_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port68_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port69_rx_los.dev_attr.attr,
	&sensor_dev_attr_port69_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port69_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port70_rx_los.dev_attr.attr,
	&sensor_dev_attr_port70_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port70_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port71_rx_los.dev_attr.attr,
	&sensor_dev_attr_port71_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port71_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port72_rx_los.dev_attr.attr,
	&sensor_dev_attr_port72_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port72_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port73_rx_los.dev_attr.attr,
	&sensor_dev_attr_port73_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port73_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port74_rx_los.dev_attr.attr,
	&sensor_dev_attr_port74_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port74_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port75_rx_los.dev_attr.attr,
	&sensor_dev_attr_port75_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port75_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port76_rx_los.dev_attr.attr,
	&sensor_dev_attr_port76_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port76_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port77_rx_los.dev_attr.attr,
	&sensor_dev_attr_port77_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port77_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port78_rx_los.dev_attr.attr,
	&sensor_dev_attr_port78_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port78_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port79_rx_los.dev_attr.attr,
	&sensor_dev_attr_port79_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port79_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port80_rx_los.dev_attr.attr,
	&sensor_dev_attr_port80_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port80_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port81_rx_los.dev_attr.attr,
	&sensor_dev_attr_port81_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port81_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port82_rx_los.dev_attr.attr,
	&sensor_dev_attr_port82_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port82_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port83_rx_los.dev_attr.attr,
	&sensor_dev_attr_port83_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port83_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port84_rx_los.dev_attr.attr,
	&sensor_dev_attr_port84_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port84_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port85_rx_los.dev_attr.attr,
	&sensor_dev_attr_port85_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port85_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port86_rx_los.dev_attr.attr,
	&sensor_dev_attr_port86_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port86_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port87_rx_los.dev_attr.attr,
	&sensor_dev_attr_port87_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port87_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port88_rx_los.dev_attr.attr,
	&sensor_dev_attr_port88_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port88_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port89_rx_los.dev_attr.attr,
	&sensor_dev_attr_port89_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port89_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port90_rx_los.dev_attr.attr,
	&sensor_dev_attr_port90_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port90_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port91_rx_los.dev_attr.attr,
	&sensor_dev_attr_port91_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port91_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port92_rx_los.dev_attr.attr,
	&sensor_dev_attr_port92_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port92_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port93_rx_los.dev_attr.attr,
	&sensor_dev_attr_port93_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port93_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port94_rx_los.dev_attr.attr,
	&sensor_dev_attr_port94_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port94_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port95_rx_los.dev_attr.attr,
	&sensor_dev_attr_port95_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port95_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port96_rx_los.dev_attr.attr,
	&sensor_dev_attr_port96_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port96_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port97_rx_los.dev_attr.attr,
	&sensor_dev_attr_port97_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port97_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port98_rx_los.dev_attr.attr,
	&sensor_dev_attr_port98_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port98_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port99_rx_los.dev_attr.attr,
	&sensor_dev_attr_port99_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port99_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port100_rx_los.dev_attr.attr,
	&sensor_dev_attr_port100_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port100_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port101_rx_los.dev_attr.attr,
	&sensor_dev_attr_port101_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port101_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port102_rx_los.dev_attr.attr,
	&sensor_dev_attr_port102_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port102_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port103_rx_los.dev_attr.attr,
	&sensor_dev_attr_port103_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port103_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port104_rx_los.dev_attr.attr,
	&sensor_dev_attr_port104_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port104_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port105_rx_los.dev_attr.attr,
	&sensor_dev_attr_port105_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port105_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port106_rx_los.dev_attr.attr,
	&sensor_dev_attr_port106_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port106_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port107_rx_los.dev_attr.attr,
	&sensor_dev_attr_port107_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port107_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port108_rx_los.dev_attr.attr,
	&sensor_dev_attr_port108_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port108_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port109_rx_los.dev_attr.attr,
	&sensor_dev_attr_port109_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port109_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port110_rx_los.dev_attr.attr,
	&sensor_dev_attr_port110_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port110_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port111_rx_los.dev_attr.attr,
	&sensor_dev_attr_port111_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port111_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port112_rx_los.dev_attr.attr,
	&sensor_dev_attr_port112_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port112_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port113_rx_los.dev_attr.attr,
	&sensor_dev_attr_port113_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port113_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port114_rx_los.dev_attr.attr,
	&sensor_dev_attr_port114_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port114_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port115_rx_los.dev_attr.attr,
	&sensor_dev_attr_port115_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port115_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port116_rx_los.dev_attr.attr,
	&sensor_dev_attr_port116_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port116_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port117_rx_los.dev_attr.attr,
	&sensor_dev_attr_port117_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port117_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port118_rx_los.dev_attr.attr,
	&sensor_dev_attr_port118_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port118_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port119_rx_los.dev_attr.attr,
	&sensor_dev_attr_port119_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port119_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port120_rx_los.dev_attr.attr,
	&sensor_dev_attr_port120_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port120_tx_disable.dev_attr.attr,

	&sensor_dev_attr_port121_rx_los.dev_attr.attr,
	&sensor_dev_attr_port121_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port121_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port122_rx_los.dev_attr.attr,
	&sensor_dev_attr_port122_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port122_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port123_rx_los.dev_attr.attr,
	&sensor_dev_attr_port123_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port123_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port124_rx_los.dev_attr.attr,
	&sensor_dev_attr_port124_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port124_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port125_rx_los.dev_attr.attr,
	&sensor_dev_attr_port125_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port125_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port126_rx_los.dev_attr.attr,
	&sensor_dev_attr_port126_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port126_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port127_rx_los.dev_attr.attr,
	&sensor_dev_attr_port127_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port127_tx_disable.dev_attr.attr,
	&sensor_dev_attr_port128_rx_los.dev_attr.attr,
	&sensor_dev_attr_port128_tx_fault.dev_attr.attr,
	&sensor_dev_attr_port128_tx_disable.dev_attr.attr,

	NULL,
};

static const struct attribute_group port_cpld_attr_group = {
	.attrs = port_cpld_attributes,
};

static int port_cpld_probe(struct i2c_client *client, const struct i2c_device_id *dev_id)
{
	int ret;
	struct cpld_data *data;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "i2c_check_functionality failed (0x%x)\n", client->addr);
		return -EIO;
	}

#if 0
	byte_date = i2c_smbus_read_byte_data(client, VERIFY_REG);
	if (byte_date != VERIFY_REG_VALUE) {
		dev_err(&client->dev, "the port1-probe can not fand the corresponding hardware "
			"device(0x%x) read value(0x%02x) from reg(0x%02x)\n",
			client->addr, byte_date, VERIFY_REG);
		//return -1;
	}
#endif /* vertual device driver */

	data = kzalloc(sizeof(struct cpld_data), GFP_KERNEL);

	if (!data) {
		dev_err(&client->dev, "Can't allocate cpld_data (0x%x)\n", client->addr);
		return -ENOMEM;
	}

	data->client = client;
	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	ret = sysfs_create_group(&client->dev.kobj, &port_cpld_attr_group);
	if (ret < 0) {
		dev_err(&client->dev, "Cannot create sysfs\n");
		goto exit;
	}

	dev_info(&client->dev, "cpld misc probe success\n");

	return 0;

exit:
	kfree(data);
	return ret;
}

static int port_cpld_remove(struct i2c_client *client)
{
	struct cpld_data *data = i2c_get_clientdata(client);

	sysfs_remove_group(&client->dev.kobj, &port_cpld_attr_group);
	kfree(data);

	dev_info(&client->dev, "cpld misc remove success\n");
	return 0;
}

static const struct i2c_device_id cpld_id[] = {
	{ "cpld-misc", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, cpld_id);

static struct i2c_driver cpld_driver = {
	.driver = {
		.name = "cpld-misc",
	},
	.probe       = port_cpld_probe,
	.remove      = port_cpld_remove,
	.id_table    = cpld_id,
};

module_i2c_driver(cpld_driver);

MODULE_AUTHOR("Liwenlong");
MODULE_DESCRIPTION("Inspur SC5631EL-48Y8C-BMC PORT_CPLD1 Driver");
MODULE_LICENSE("GPL");
