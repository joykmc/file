/*
 * fpga_sff.c - driver for sc5610el Daminglake FPGA DOM auto-polling function.
 *
 * Author: baotuspringswitch baotuspringswitch@gmail.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <uapi/linux/stat.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>
#include <linux/hwmon-sysfs.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/kthread.h>

#include "daminglake_fpga_sff.h"

static int sff_xfer_mode = XFER_POLLING;
module_param(sff_xfer_mode, int, 0644);
MODULE_PARM_DESC(sff_xfer_mode,
        "FPGA sff transfer type(0:polling, 1:interrupt)");

static int dom_polling_en = 0;
module_param(dom_polling_en, int, 0644);
MODULE_PARM_DESC(dom_polling_en,
        "FPGA DOM polling enable(0:disable, 1:enable)");

static int dom_polling_delay = 100;
module_param(dom_polling_delay, int, 0644);
MODULE_PARM_DESC(dom_polling_delay,
        "FPGA DOM polling delay(ms)");

static int page_len = 8;
static int dom_page_code[OPTOE_PAGE_NUM + 3] = {0};
module_param_array(dom_page_code, int, &page_len, 0644);
MODULE_PARM_DESC(dom_page_code, "FPGA DOM polling page number, max support 8 pages(0~255)");

struct task_struct *fpga_dom_tsk[OPTOE_NUM];

static ssize_t show_fpga_dom_bus_hang_sta(struct device *dev, struct device_attribute *attr, char *buf)
{
         /*To do: Add i2c bus hang sysfs node*/
        return -1;
}

static ssize_t store_fpga_dom_bus_hang_sta(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
         /*To do: Add i2c bus hang sysfs node*/
        return count;
}

static DEVICE_ATTR(fpga_dom_bus_hang_sta, 0664, show_fpga_dom_bus_hang_sta, store_fpga_dom_bus_hang_sta);

static struct attribute *i2c_fpga_dom_attrs[] = {
    &dev_attr_fpga_dom_bus_hang_sta.attr,
    NULL,
};

static struct attribute_group i2c_fpga_dom_attr_grp = {
        .attrs = i2c_fpga_dom_attrs,
};

static int fpga_dom_bus_reset(struct i2c_adapter *adap) {
    //To do: Add i2c bus auto reeset function
    return -1;
}

static int eeprom_buffer_copy(void *data)
{
    int ctrl_code = 0;
    int page, count, addr;
    int error = 0;
    int status, done_flag;
    struct daminglake_fpga_dom_reg_addr dom_reg;
    struct daminglake_fpga_dom_priv *priv = data;
    void __iomem *base_addr = priv->base_addr;
    void __iomem *data_addr;
    int port_num = priv->port_num;

    char data_info[OPTOE_POLL_PRE_SIZE] = {""};

    dom_reg.reg_poll_ctl = base_addr + FPGA_OPTOE1_POL_CFG;
    dom_reg.reg_poll_sta = base_addr + FPGA_OPTOE1_POL_STA;
    iowrite8(OPTOE_RW_BIT | OPTOE_EN_BIT, dom_reg.reg_poll_sta);

    while(!kthread_should_stop())
    {
        if (!dom_polling_en) {
            msleep(dom_polling_delay);
            continue;
        }

        for(page = 0; page < (OPTOE_PAGE_NUM + 1); page++) {
            if ((page != 0) && (page != 1) && (!dom_page_code[page])){
                continue;
            }

            if (page == 0) {
                ctrl_code = 0;
                if (dom_page_code[page] !=  0) {
                    dom_page_code[page] = 0;
                }
            } else if (page == 1) {
                if (dom_page_code[page] !=  0) {
                    dom_page_code[page] = 0;
                }
                ctrl_code = OPTOE_PAGE_CTL + dom_page_code[page];
            } else {
                if ((dom_page_code[page] < 0) || (dom_page_code[page] > (OPTOE_PAGE_NUM - 1))) {
                    continue;
                }
                ctrl_code = OPTOE_PAGE_CTL + dom_page_code[page];
            }

            mutex_lock(&priv->polling_lock);
            iowrite16(ctrl_code, dom_reg.reg_poll_ctl);
            iowrite8(OPTOE_RW_BIT | OPTOE_EN_BIT, dom_reg.reg_poll_sta);
            mutex_unlock(&priv->polling_lock);
            dev_dbg_ratelimited(&priv->dom_dev, "Set port %d ctrl code 0x%x\n", port_num, ctrl_code);

            msleep(5);
            if (page == 0) {
                data_addr = dom_reg.reg_poll_data;
            } else {
                data_addr = dom_reg.reg_poll_data + OPTOE_RAM_UPPER_ADDR_GAP;
            }

            error = readx_poll_timeout_atomic(ioread8, (dom_reg.reg_poll_sta), status, status & OPTOE_DONE_BIT, 100, 5000);
            if(error < 0) {
                dev_err_ratelimited(&priv->dom_dev, "port: %d, Status %2.2X\n", port_num, status);
                memset(data_info, I2C_CLR_REG_VAL, OPTOE_POLL_PRE_SIZE);

                mutex_lock(&priv->polling_lock);
                if ((port_num < QSFP_MOD_OFF) && addr) {
                    memcpy(priv->eeprom_info + (page + 2) * OPTOE_POLL_PRE_SIZE, data_info, OPTOE_POLL_PRE_SIZE);
                } else {
                    memcpy(priv->eeprom_info + page * OPTOE_POLL_PRE_SIZE, data_info, OPTOE_POLL_PRE_SIZE);
                }

                iowrite8(OPTOE_TIP_BIT, dom_reg.reg_poll_sta);
                mutex_unlock(&priv->polling_lock);
            }

            if (!(status & OPTOE_RD_BIT)) {
                dev_err_ratelimited(&priv->dom_dev, "port: %d, Status %2.2X, error: %2.2X, EEPROM read error!\n", port_num, status, done_flag);
                memset(data_info, I2C_CLR_REG_VAL, OPTOE_POLL_PRE_SIZE);

                mutex_lock(&priv->polling_lock);
                if ((port_num < QSFP_MOD_OFF) && addr) {
                    memcpy(priv->eeprom_info + (page + 2) * OPTOE_POLL_PRE_SIZE, data_info, OPTOE_POLL_PRE_SIZE);
                } else {
                    memcpy(priv->eeprom_info + page * OPTOE_POLL_PRE_SIZE, data_info, OPTOE_POLL_PRE_SIZE);
                }

                iowrite8(OPTOE_TIP_BIT, dom_reg.reg_poll_sta);
                mutex_unlock(&priv->polling_lock);
            }

            for(count = 0; count < OPTOE_POLL_PRE_SIZE; count++) {
                data_info[count] = ioread8(data_addr + count);
            }

            iowrite8(OPTOE_TIP_BIT, dom_reg.reg_poll_sta);

            mutex_lock(&priv->polling_lock);
            if ((port_num <QSFP_MOD_OFF) && addr) {
                memcpy(priv->eeprom_info + (page + 2) * OPTOE_POLL_PRE_SIZE, data_info, OPTOE_POLL_PRE_SIZE);
            } else {
                memcpy(priv->eeprom_info + page * OPTOE_POLL_PRE_SIZE, data_info, OPTOE_POLL_PRE_SIZE);
            }

            mutex_unlock(&priv->polling_lock);
            memset(data_info, '\0', OPTOE_POLL_PRE_SIZE);
        }

        msleep(dom_polling_delay);
    }
    return 0;
}

static ssize_t fpga_dom_bin_read(struct file *filp, struct kobject *kobj,
                struct bin_attribute *attr,
                char *buf, loff_t off, size_t count)
{
    int start_page, end_page, page, page_count, flag;
    loff_t page_start_off = 0, page_end_off = 0, page_off = 0;
    size_t pending_len = 0, page_len = 0;
    struct device *odev = kobj_to_dev(kobj);
    struct daminglake_fpga_dom_priv *priv = dev_get_drvdata(odev);
    int port_num = priv->port_num;
    pending_len = count;

    flag = 0;
    start_page = off >> 7;
    end_page = (off + count - 1) >> 7;

    for (page = start_page; page <= end_page; page++) {
        page_start_off = page * OPTOE_POLL_PRE_SIZE;
        page_end_off = (page + 1) * OPTOE_POLL_PRE_SIZE;
        if (page_start_off < off) {
            page_off = off;
            if ((off + pending_len) < page_end_off) {
                page_len = pending_len;
            } else {
                page_len = page_end_off - off;
            }
        } else {
            page_off = 0;
            if (pending_len < OPTOE_POLL_PRE_SIZE) {
                page_len = pending_len;
            } else {
                page_len = OPTOE_POLL_PRE_SIZE;
            }
        }

        if (page < 2) {
            mutex_lock(&priv->polling_lock);
            memcpy(buf, priv->eeprom_info + page * OPTOE_POLL_PRE_SIZE + page_off, page_len);
            mutex_unlock(&priv->polling_lock);
        } else {
            if (port_num > QSFP_MOD_OFF) {
                for(page_count = 2; page_count < (OPTOE_PAGE_NUM + 1); page_count++) {
                    if (dom_page_code[page_count] == (page - 1)) {
                        mutex_lock(&priv->polling_lock);
                        memcpy(buf, priv->eeprom_info + page_count * OPTOE_POLL_PRE_SIZE + page_off, page_len);
                        mutex_unlock(&priv->polling_lock);         
                        flag = 1;
                    }
                }
            } else {
                if (page < 4) {
                    mutex_lock(&priv->polling_lock);
                    memcpy(buf, priv->eeprom_info + page * OPTOE_POLL_PRE_SIZE + page_off, page_len);
                    mutex_unlock(&priv->polling_lock);
                    flag = 1;
                } else {
                    for(page_count = 2; page_count < (OPTOE_PAGE_NUM + 3); page_count++) {
                        if (dom_page_code[page_count] == (page - 3)) {
                            mutex_lock(&priv->polling_lock);
                            memcpy(buf, priv->eeprom_info + (page_count + 2)  * OPTOE_POLL_PRE_SIZE + page_off, page_len);
                            mutex_unlock(&priv->polling_lock);       
                            flag = 1;
                        }
                    }
                }
            }

            if (flag) {
                flag = 0;
            } else {
                memset(buf, I2C_CLR_REG_VAL, page_len);
            }
        }
        buf += page_len;
        pending_len -= page_len;
    }

    return count;
}

static const struct bin_attribute dom_eeprom_bin = {
        .attr = { .name = "dom_eeprom", .mode = 0644 },
        .size = (OPTOE_PAGE_NUM + 3) * OPTOE_POLL_PRE_SIZE,
        .read = fpga_dom_bin_read,
};

static void fpga_to_sff_int_process(struct daminglake_fpga_dom_priv *dev_data)
{
    int ctrl_code = 0;
    int count, page_num;
    int page = dev_data->polling_page;
    int status;
    int max_page = dev_data->max_page;
    struct daminglake_fpga_dom_reg_addr dom_reg;
    void __iomem *base_addr = dev_data->base_addr;
    void __iomem *data_addr;
    int port_num = dev_data->port_num;

    char data_info[OPTOE_POLL_PRE_SIZE] = {""};

    dom_reg.reg_poll_ctl = base_addr + FPGA_OPTOE1_POL_CFG;
    dom_reg.reg_poll_sta = base_addr + FPGA_OPTOE1_POL_STA;

    status = ioread8(dom_reg.reg_poll_sta);
    if (!(status & OPTOE_DONE_BIT)) {
        return;
    }
    iowrite8(OPTOE_TIP_BIT, dom_reg.reg_poll_sta);

    for(page_num = 0; page_num < (OPTOE_PAGE_NUM + 1); page_num++) {
        if ((page_num == 0) || (page_num == 1)) {
            continue;
        }
        if (page_num == 2) {
            if (dom_page_code[page_num] == 0) {
                max_page = 2;
                break;
            } else {
                continue;
            }
        }

        if (dom_page_code[page_num] == 0) {
            max_page = page_num - 1;
            break;
        }
    }

    if (port_num < QSFP_MOD_OFF) {
        max_page = max_page + 2;
    }
    dev_data->max_page = max_page;

    if (port_num > (QSFP_MOD_OFF - 1)) {
        if ((page != 0) && (page != 1) && (!dom_page_code[page])){
            page = page + 1;
            if (page > max_page) {
                page = 0;
	        }
        }
        return;
    } else {
        if ((page < 4) && (!dom_page_code[page])){
            page = page + 1;
            if (page > max_page) {
                page = 0;
	        }
        }
        return;
    }
    
    if (!(status & OPTOE_RD_BIT)) {
        memset(data_info, 0, OPTOE_POLL_PRE_SIZE);

	    mutex_lock(&dev_data->polling_lock);
        memcpy(dev_data->eeprom_info + page * OPTOE_POLL_PRE_SIZE, data_info, OPTOE_POLL_PRE_SIZE);
        iowrite8(OPTOE_TIP_BIT, dom_reg.reg_poll_sta);
        mutex_unlock(&dev_data->polling_lock);
        return;
    }

    if ((page == 0) || ((page == 2) && (port_num < QSFP_MOD_OFF))) {
        data_addr = dom_reg.reg_poll_data; 
    } else {
        data_addr = dom_reg.reg_poll_data + OPTOE_RAM_UPPER_ADDR_GAP; 
    }
    memset(data_info, '\0', OPTOE_POLL_PRE_SIZE);

    for(count = 0; count < OPTOE_POLL_PRE_SIZE; count++) {
        data_info[count] = ioread8(data_addr + count);
    }

    mutex_lock(&dev_data->polling_lock);
    memcpy(dev_data->eeprom_info + page * OPTOE_POLL_PRE_SIZE, data_info, OPTOE_POLL_PRE_SIZE);
    mutex_unlock(&dev_data->polling_lock);
    memset(data_info, '\0', OPTOE_POLL_PRE_SIZE);

    page = page + 1;
    if (page > max_page) {
        page = 0;
    }
    
    dev_data->polling_page = page;

    if (page == 0) {
        ctrl_code = 0;
        if (dom_page_code[page] !=  0) {
            dom_page_code[page] = 0;
        }
    } else if (page == 1) {
        if (dom_page_code[page] !=  0) {
            dom_page_code[page] = 0;
        }
        ctrl_code = OPTOE_PAGE_CTL + dom_page_code[page];
    } else if ((page == 2) && (port_num < QSFP_MOD_OFF)) {
        ctrl_code = 0;
    } else if ((page == 3) && (port_num < QSFP_MOD_OFF)) {
        ctrl_code = OPTOE_PAGE_CTL;
    } else if ((page > 3) && (port_num < QSFP_MOD_OFF)) {
        if ((dom_page_code[page - 2] < 0) || (dom_page_code[page - 2] > (OPTOE_PAGE_NUM - 1))) {
            return;
        }
        ctrl_code = OPTOE_PAGE_CTL + dom_page_code[page - 2];
    }
    else {
        if ((dom_page_code[page] < 0) || (dom_page_code[page] > (OPTOE_PAGE_NUM - 1))) {
            return;
        }
        ctrl_code = OPTOE_PAGE_CTL + dom_page_code[page];
    }

    iowrite16(ctrl_code, dom_reg.reg_poll_ctl);
    iowrite8(OPTOE_RW_BIT | OPTOE_EN_BIT, dom_reg.reg_poll_ctl + 4);
    return;
}

static irqreturn_t handle_sff_int(int irqno, void *dev_id)
{
   struct daminglake_fpga_dom_priv *device_data = dev_id;
   fpga_to_sff_int_process(device_data);

   return IRQ_HANDLED;
}

static int daminglake_fpga_dom_drv_probe(struct platform_device *pdev)
{
    struct resource *res[2];
    int ret = 0;
    int error, page;
    struct daminglake_fpga_dom_priv *priv;
    struct daminglake_fpga_dom_reg_addr dom_reg;

    dom_page_code[2] = 0x80;
	dom_page_code[3] = 0xfa;

    priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    res[0] = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (unlikely(!res[0])) {
        dev_err_ratelimited(&pdev->dev, "Specified Resource Not Available...\n");
        return -1;
    }
    priv->base_addr = devm_ioremap_resource(&pdev->dev, res[0]);
    dev_info_ratelimited(&pdev->dev, "Get port id %d, base addr 0x%lx\n", priv->port_num, (unsigned long)priv->base_addr);

    priv->irq = platform_get_irq(pdev, 0);

    res[1] = platform_get_resource(pdev, IORESOURCE_MEM, 1);
    if (unlikely(!res[1])) {
        dev_err_ratelimited(&pdev->dev, "Specified Resource Not Available...\n");
        return -1;
    }
    priv->ram_addr = devm_ioremap_resource(&pdev->dev, res[1]);
    dev_info_ratelimited(&pdev->dev, "Get port id %d, ram addr 0x%lx\n", priv->port_num, (unsigned long)priv->ram_addr);

    priv->max_page = 2;
    for(page = 0; page < (OPTOE_PAGE_NUM + 1); page++) {
        if ((page == 0) || (page == 1)) {
            continue;
        }
        if (page == 2) {
            if (dom_page_code[page] == 0) {
                priv->max_page = 2;
                break;
            } else {
                continue;
            }
        }

        if (dom_page_code[page] == 0) {
            priv->max_page = page - 1;
            break;
        }
    }

    if (pdev->id > (QSFP_MOD_OFF - 1)) {
        priv->eeprom_info = devm_kzalloc(&pdev->dev, (OPTOE_PAGE_NUM + 2) * OPTOE_POLL_PRE_SIZE * sizeof(char), GFP_KERNEL);
        priv->max_page = priv->max_page + 2;
    } else {
        priv->eeprom_info = devm_kzalloc(&pdev->dev, (OPTOE_PAGE_NUM + 4) * OPTOE_POLL_PRE_SIZE * sizeof(char), GFP_KERNEL);
    }
    if (!priv->eeprom_info) {
        devm_kfree(&pdev->dev, priv);
        return -ENOMEM;
    }
    
    priv->port_num = pdev->id;
    mutex_init(&priv->polling_lock);
    priv->polling_page = 0;
    priv->dom_dev = pdev->dev;

    dom_reg.reg_poll_ctl = priv->base_addr + FPGA_OPTOE1_POL_CFG;
    dom_reg.reg_poll_sta = priv->base_addr + FPGA_OPTOE1_POL_STA;
    platform_set_drvdata(pdev, priv);

    ret = sysfs_create_bin_file(&pdev->dev.kobj, &dom_eeprom_bin);
    if (ret) {
        devm_kfree(&pdev->dev, priv->eeprom_info);
        devm_kfree(&pdev->dev, priv);
        dev_err_ratelimited(&pdev->dev, "Cannot create sysfs for fpga DOM function\n");
        return ret;
    }

    ret = sysfs_create_group(&pdev->dev.kobj, &i2c_fpga_dom_attr_grp);
    if (unlikely(ret < 0)) {
        sysfs_remove_bin_file(&pdev->dev.kobj, &dom_eeprom_bin);
        devm_kfree(&pdev->dev, priv->eeprom_info);
        devm_kfree(&pdev->dev, priv);
        dev_err_ratelimited(&pdev->dev, "Optoe I2C adapter %d create sysfs fail!\n", pdev->id);
        return -ENOMEM;
    }

    if (!sff_xfer_mode) {
        fpga_dom_tsk[pdev->id] = kthread_create(eeprom_buffer_copy, priv, "optoe_thread_%d", pdev->id);
        if (fpga_dom_tsk[pdev->id]) {
            wake_up_process(fpga_dom_tsk[pdev->id]);
            dev_info_ratelimited(&pdev->dev, "kernel thread %d init probe OK.\n", pdev->id);

        } else {
            dev_err_ratelimited(&pdev->dev, "kernel thread %d init faild\n.", pdev->id);
            return -1;
        }
    }

    if (sff_xfer_mode == XFER_INTERRUPT) {
        if (priv->irq < 0) {
            devm_kfree(&pdev->dev, priv->eeprom_info);
            devm_kfree(&pdev->dev, priv);
            sysfs_remove_bin_file(&pdev->dev.kobj, &dom_eeprom_bin);
            dev_err_ratelimited(&pdev->dev, "Can not get sff to pci irq!\n");
            return -1;
        } else {
            error = devm_request_any_context_irq(&pdev->dev, priv->irq, handle_sff_int, IRQF_SHARED, pdev->name, priv);
            if (error) {
                devm_kfree(&pdev->dev, priv->eeprom_info);
                devm_kfree(&pdev->dev, priv);
                sysfs_remove_bin_file(&pdev->dev.kobj, &dom_eeprom_bin);
                dev_err_ratelimited(&pdev->dev, "Get i2c %d irq req fail! get %d\n", priv->irq , error);
                return -1;
            }
        }

        iowrite8(I2C_CLR_REG_VAL, dom_reg.reg_poll_ctl - FPGA_OPTOE_RTC_GAP + FPGA_OPTOE1_RTC_STA);
        iowrite16(I2C_CLR_REG_VAL, dom_reg.reg_poll_ctl);
        iowrite8(OPTOE_RW_BIT | OPTOE_EN_BIT, dom_reg.reg_poll_ctl + FPGA_OPTOE1_POL_STA);
    }

    return 0;
}

static int daminglake_fpga_dom_drv_remove(struct platform_device *pdev)
{
    struct daminglake_fpga_dom_priv *priv = platform_get_drvdata(pdev);
    void __iomem *ctrl_addr;
    dev_err_ratelimited(&pdev->dev, "Stop %d port auto-polling\n", pdev->id);

    if ((dom_polling_en) || (!sff_xfer_mode)) {
        kthread_stop(fpga_dom_tsk[pdev->id]);
    }

    if (sff_xfer_mode == XFER_INTERRUPT) {
        ctrl_addr = priv->base_addr + FPGA_OPTOE1_POL_STA;
        iowrite8(OPTOE_TIP_BIT, ctrl_addr);
        devm_free_irq(&pdev->dev, priv->irq, priv);
    }

    devm_iounmap(&pdev->dev, priv->base_addr);
    devm_iounmap(&pdev->dev, priv->ram_addr);
    devm_kfree(&pdev->dev, priv->eeprom_info);
    devm_kfree(&pdev->dev, priv);
    sysfs_remove_group(&pdev->dev.kobj, &i2c_fpga_dom_attr_grp);
    sysfs_remove_bin_file(&pdev->dev.kobj, &dom_eeprom_bin);

    return 0;
}

static const struct platform_device_id daminglake_fpga_dom_driver_id[] = {
    {.name = "fpga_dom_polling_dev",},
    {}
};

static struct platform_driver daminglake_fpga_dom_drv = {
        .probe  = daminglake_fpga_dom_drv_probe,
        .remove = daminglake_fpga_dom_drv_remove,
        .driver = {
                .name   = "daminglake_fpga_dom_driver",
        },
    .id_table = daminglake_fpga_dom_driver_id,
};

module_platform_driver(daminglake_fpga_dom_drv);

MODULE_AUTHOR("baotuspringswitch baotuspringswitch@gmail.com");
MODULE_DESCRIPTION("Daminglake fpga DOM auto-polling driver");
MODULE_LICENSE("GPL");


