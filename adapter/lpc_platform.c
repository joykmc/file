/* **
 * i2c_opencore_driver.c - driver for PORT_CPLD i2c adapter.
 * and the module used to register eight i2c-opencore-x device.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * */

#include <linux/init.h>
#include <asm/ptrace.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>

#include "i2c_ocores.h"

static void port_cpld_dev_release(struct device *dev)
{
    printk("release i2c OpenCoreX device.\n");
    return;
}

static struct resource i2c_opencore1_res[] = {
    {
        .start = 0x0700,
        .end = 0x0707,
        .flags = IORESOURCE_IO,
    },
};

static struct platform_device i2c_opencore1_device = {
    .name = OCORES_DEVICE1_NAME,
    .id = -1,
    .num_resources = ARRAY_SIZE(i2c_opencore1_res),
    .resource = i2c_opencore1_res,
    .dev = {
        .release = port_cpld_dev_release,
    }
};


static struct resource i2c_opencore2_res[] = {
    {
        .start = 0x0708,
        .end = 0x070f,
        .flags = IORESOURCE_IO,
    },
};

static struct platform_device i2c_opencore2_device = {
    .name = OCORES_DEVICE2_NAME,
    .id = -1,
    .num_resources = ARRAY_SIZE(i2c_opencore2_res),
    .resource = i2c_opencore2_res,
    .dev = {
        .release = port_cpld_dev_release,
    }
};


static struct resource i2c_opencore3_res[] = {
    {
        .start = 0x0710,
        .end = 0x0717,
        .flags = IORESOURCE_IO,
    },
};

static struct platform_device i2c_opencore3_device = {
    .name = OCORES_DEVICE3_NAME,
    .id = -1,
    .num_resources = ARRAY_SIZE(i2c_opencore3_res),
    .resource = i2c_opencore3_res,
    .dev = {
        .release = port_cpld_dev_release,
    }
};


static struct resource i2c_opencore4_res[] = {
    {
        .start = 0x0718,
        .end = 0x071f,
        .flags = IORESOURCE_IO,
    },
};

static struct platform_device i2c_opencore4_device = {
    .name = OCORES_DEVICE4_NAME,
    .id = -1,
    .num_resources = ARRAY_SIZE(i2c_opencore4_res),
    .resource = i2c_opencore4_res,
    .dev = {
        .release = port_cpld_dev_release,
    }
};

/* i2c_opencore5~8 in port_cpld2 flash */
static struct resource i2c_opencore5_res[] = {
    {
        .start = 0x0900,
        .end = 0x0907,
        .flags = IORESOURCE_IO,
    },
};

static struct platform_device i2c_opencore5_device = {
    .name = OCORES_DEVICE5_NAME,
    .id = -1,
    .num_resources = ARRAY_SIZE(i2c_opencore5_res),
    .resource = i2c_opencore5_res,
    .dev = {
        .release = port_cpld_dev_release,
    }
};

static struct resource i2c_opencore6_res[] = {
    {
        .start = 0x0908,
        .end = 0x090f,
        .flags = IORESOURCE_IO,
    },
};

static struct platform_device i2c_opencore6_device = {
    .name = OCORES_DEVICE6_NAME,
    .id = -1,
    .num_resources = ARRAY_SIZE(i2c_opencore6_res),
    .resource = i2c_opencore6_res,
    .dev = {
        .release = port_cpld_dev_release,
    }
};

static struct resource i2c_opencore7_res[] = {
    {
        .start = 0x0910,
        .end = 0x0917,
        .flags = IORESOURCE_IO,
    },
};

static struct platform_device i2c_opencore7_device = {
    .name = OCORES_DEVICE7_NAME,
    .id = -1,
    .num_resources = ARRAY_SIZE(i2c_opencore7_res),
    .resource = i2c_opencore7_res,
    .dev = {
        .release = port_cpld_dev_release,
    }
};

static struct resource i2c_opencore8_res[] = {
    {
        .start = 0x0918,
        .end = 0x091f,
        .flags = IORESOURCE_IO,
    },
};

static struct platform_device i2c_opencore8_device = {
    .name = OCORES_DEVICE8_NAME,
    .id = -1,
    .num_resources = ARRAY_SIZE(i2c_opencore8_res),
    .resource = i2c_opencore8_res,
    .dev = {
        .release = port_cpld_dev_release,
    }
};

static struct resource port_cpld_res[] = {
    {
        .start = 0x0700,
        .end = 0x091f,
        .flags = IORESOURCE_IO,
    },
};

static struct platform_device port_cpld_device = {
    .name = PORT_CPLD_NAME,
    .id = -1,
    .num_resources = ARRAY_SIZE(port_cpld_res),
    .resource = port_cpld_res,
    .dev = {
        .release = port_cpld_dev_release,
    }
};

int port_cpld_probe(void)
{
    /* TODO:
     * check that the driver module is being register when the device exists */
    /* register i2c master OpenCore1 */
    platform_device_register(&i2c_opencore1_device);
    /* register i2c master OpenCore2 */
    platform_device_register(&i2c_opencore2_device);
    /* register i2c master OpenCore3 */
    platform_device_register(&i2c_opencore3_device);
    /* register i2c master OpenCore4 */
    platform_device_register(&i2c_opencore4_device);

    /* register i2c master OpenCore5 */
    platform_device_register(&i2c_opencore5_device);
    /* register i2c master OpenCore6 */
    platform_device_register(&i2c_opencore6_device);
    /* register i2c master OpenCore7 */
    platform_device_register(&i2c_opencore7_device);
    /* register i2c master OpenCore8 */
    platform_device_register(&i2c_opencore8_device);
    /* register port cpld device for hitless */
    platform_device_register(&port_cpld_device);
    return 0;
}

void port_cpld_remove(void)
{
    /* unregister port cpld device for hitless */
    platform_device_unregister(&port_cpld_device);
    /* unregister i2c master OpenCore8 */
    platform_device_unregister(&i2c_opencore8_device);
    /* unregister i2c master OpenCore7 */
    platform_device_unregister(&i2c_opencore7_device);
    /* unregister i2c master OpenCore6 */
    platform_device_unregister(&i2c_opencore6_device);
    /* unregister i2c master OpenCore5 */
    platform_device_unregister(&i2c_opencore5_device);

    /* unregister i2c master OpenCore4 */
    platform_device_unregister(&i2c_opencore4_device);
    /* unregister i2c master OpenCore3 */
    platform_device_unregister(&i2c_opencore3_device);
    /* unregister i2c master OpenCore2 */
    platform_device_unregister(&i2c_opencore2_device);
    /* unregister i2c master OpenCore1 */
    platform_device_unregister(&i2c_opencore1_device);
}

module_init(port_cpld_probe);
module_exit(port_cpld_remove);

MODULE_AUTHOR("baotuspringswitch@gmail.com");
MODULE_DESCRIPTION("Port CPLD register device for I2C OpenCore");
MODULE_LICENSE("GPL");

