/*
 * fpga_sff_rtc.c - driver for Daminglake FPGA optoe RTC emul i2c adapter.
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

#include "daminglake_fpga_sff_rtc.h"

static int sff_xfer_mode = XFER_POLLING;
module_param(sff_xfer_mode, int, 0644);
MODULE_PARM_DESC(sff_xfer_mode,
        "FPGA sff transfer type(0:polling, 1:interrupt)");

static int i2c_bus_base = 0;
module_param(i2c_bus_base, int, 0644);
MODULE_PARM_DESC(i2c_bus_base,
        "FPGA RTC emul i2c adapter base number.");

static int fpga_rtc_emul_recover_bus(struct i2c_adapter *adap) {
    //To do: Add emul i2c bus auto clear function
    return -1;
}

static int fpga_rtc_emul_bus_reset(struct i2c_adapter *adap) {
    //To do: Add emul i2c bus auto reeset function
    return -1;
}

static int fpga_rtc_emul_get_scl(struct i2c_adapter *adap) {
    //To do: Add emul i2c bus auto clear function
    return -1;
}

static void fpga_rtc_emul_set_scl(struct i2c_adapter *adap, int val) {
    //To do: Add emul i2c bus auto clear function
}

static int fpga_rtc_emul_get_sda(struct i2c_adapter *adap) {
    //To do: Add emul i2c bus auto clear function
    return -1;
}

static struct i2c_bus_recovery_info fpga_rtc_emul_bus_recovery_info = {
	.get_scl		= fpga_rtc_emul_get_scl,
	.get_sda		= fpga_rtc_emul_get_sda,
	.set_scl		= fpga_rtc_emul_set_scl,
	.recover_bus		= fpga_rtc_emul_recover_bus,
};

/* OPTOE EMUL SMBUS algorithm */
static int rtc_emul_i2c_poll(char rw, u8 cmd, int size, struct daminglake_fpga_rtc_reg_addr rtc_reg,
                         union i2c_smbus_data *data, struct i2c_adapter *adapter)
{
    int error = 0;
    int cnt = 0;
    int bid = 0;
    int status = 0;

    error = readx_poll_timeout_atomic(ioread8, rtc_reg.reg_rtc_sta, status, status & OPTOE_DONE_BIT, 10, 10000);
    if(error < 0) {
        dev_dbg_ratelimited(&adapter->dev, "Status %2.2X", status);
        goto Done;
    }

    if( rw == I2C_SMBUS_WRITE && (
            size == I2C_SMBUS_BYTE ||
            size == I2C_SMBUS_BYTE_DATA ||
            size == I2C_SMBUS_WORD_DATA ||
            size == I2C_SMBUS_BLOCK_DATA ||
            size == I2C_SMBUS_I2C_BLOCK_DATA
        )){
            goto Done;
        }

    if( rw == I2C_SMBUS_READ && (
            size == I2C_SMBUS_BYTE ||
            size == I2C_SMBUS_BYTE_DATA ||
            size == I2C_SMBUS_WORD_DATA ||
            size == I2C_SMBUS_BLOCK_DATA ||
            size == I2C_SMBUS_I2C_BLOCK_DATA
        )){

        switch(size){
            case I2C_SMBUS_BYTE:
            case I2C_SMBUS_BYTE_DATA:
                    cnt = 1;  break;
            case I2C_SMBUS_WORD_DATA:
                    cnt = 2;  break;
            case I2C_SMBUS_BLOCK_DATA:
            // will be changed after recived first data
                    cnt = 3;  break;
            case I2C_SMBUS_I2C_BLOCK_DATA:
                    cnt = data->block[0];  break;
            default:
                    cnt = 0;  break;
            }

            bid = 0;
            dev_dbg_ratelimited(&adapter->dev, "MS Receive");

            for(;bid<cnt;bid++){
                if(size == I2C_SMBUS_I2C_BLOCK_DATA){
                    // block[0] is read length
                    data->block[bid+1] = ioread8(rtc_reg.reg_rtc_data + cmd + bid);
                }else {
                    data->block[bid] = ioread8(rtc_reg.reg_rtc_data + cmd + bid);
                }
                dev_dbg_ratelimited(&adapter->dev, "DATA IN [%d] %2.2X",bid,data->block[bid]);
            }
        }

Done:
    iowrite8(OPTOE_TIP_BIT, rtc_reg.reg_rtc_sta);
    dev_dbg_ratelimited(&adapter->dev, "MS STOP");
    dev_dbg_ratelimited(&adapter->dev, "END --- Error code  %d", error);

    return error;
}

static void emul_rtc_int_process(struct daminglake_rtc_emul_i2c_dev_data *dev_data)
{
    union i2c_smbus_data *data = dev_data->data;
    struct daminglake_fpga_rtc_reg_addr rtc_reg;
    u8 status;
    int cnt = 0;
    int bid = 0;

    if (dev_data->state == STATE_DONE) {
        return;
    }
    //mutex_lock(&dev_data->fpga_rtc_lock);

    rtc_reg.reg_rtc_ctl = dev_data->base_addr + FPGA_OPTOE1_RTC_CFG;
    rtc_reg.reg_rtc_sta = dev_data->base_addr + FPGA_OPTOE1_RTC_STA;
    rtc_reg.reg_rtc_data = dev_data->ram_addr + FPGA_OPTOE1_RTC_RAM_REG;

    status = ioread8(rtc_reg.reg_rtc_sta);
    if (!(status & OPTOE_DONE_BIT)) {
        dev_data->state = STATE_DONE;
        iowrite8(OPTOE_TIP_BIT, rtc_reg.reg_rtc_sta);
        wake_up(&dev_data->wait);
        return;
    }

    iowrite8(OPTOE_TIP_BIT, rtc_reg.reg_rtc_sta);

    if (dev_data->state == STATE_PROCESS) {
        if( dev_data->rw == I2C_SMBUS_WRITE && (
            dev_data->size == I2C_SMBUS_BYTE ||
            dev_data->size == I2C_SMBUS_BYTE_DATA ||
            dev_data->size == I2C_SMBUS_WORD_DATA ||
            dev_data->size == I2C_SMBUS_BLOCK_DATA ||
            dev_data->size == I2C_SMBUS_I2C_BLOCK_DATA
        )){
            dev_data->state = STATE_DONE;
            iowrite8(OPTOE_TIP_BIT, rtc_reg.reg_rtc_sta);
            wake_up(&dev_data->wait);

            return;
        }

        if( dev_data->rw == I2C_SMBUS_READ && (
            dev_data->size == I2C_SMBUS_BYTE ||
            dev_data->size == I2C_SMBUS_BYTE_DATA ||
            dev_data->size == I2C_SMBUS_WORD_DATA ||
            dev_data->size == I2C_SMBUS_BLOCK_DATA ||
            dev_data->size == I2C_SMBUS_I2C_BLOCK_DATA
        )){

        switch(dev_data->size){
            case I2C_SMBUS_BYTE:
            case I2C_SMBUS_BYTE_DATA:
                    cnt = 1;  break;
            case I2C_SMBUS_WORD_DATA:
                    cnt = 2;  break;
            case I2C_SMBUS_BLOCK_DATA:
            // will be changed after recived first data
                    cnt = 3;  break;
            case I2C_SMBUS_I2C_BLOCK_DATA:
                    cnt = data->block[0];  break;
            default:
                    cnt = 0;  break;
            }

            bid = 0;

            for(;bid<cnt;bid++){
                if(dev_data->size == I2C_SMBUS_I2C_BLOCK_DATA){
                    // block[0] is read length
                    data->block[bid+1] = ioread8(rtc_reg.reg_rtc_data + dev_data->cmd + bid);
                }else {
                    data->block[bid] = ioread8(rtc_reg.reg_rtc_data + dev_data->cmd + bid);
                }
            }
        }
    }
    dev_data->state = STATE_DONE;
    wake_up(&dev_data->wait);

    return;
}

static int fpga_emul_i2c_smbus_access(struct i2c_adapter *adapter, u16 addr,
                unsigned short flags, char rw, u8 cmd,
                int size, union i2c_smbus_data *data)
{
    int error = 0;
    int cnt = 0;
    int bid = 0;
    int port_num;
    int ctrl_code;
    struct daminglake_fpga_rtc_reg_addr rtc_reg;
    struct daminglake_rtc_emul_i2c_dev_data *dev_data = i2c_get_adapdata(adapter);

    dev_dbg_ratelimited(&adapter->dev, "Smbus access start\n");
    port_num = dev_data->portid;

    rtc_reg.reg_rtc_ctl = dev_data->base_addr + FPGA_OPTOE1_RTC_CFG;
    rtc_reg.reg_rtc_sta = dev_data->base_addr + FPGA_OPTOE1_RTC_STA;
    rtc_reg.reg_rtc_data = dev_data->ram_addr + FPGA_OPTOE1_RTC_RAM_REG;
    rtc_reg.i2c_addr_reg = dev_data->base_addr + OPTOE1_ADDR_RTC_REG;

    dev_dbg_ratelimited(&adapter->dev, "portid %2d|@ 0x%2.2X|f 0x%4.4X|(%d)%-5s| (%d)%-15s|CMD %2.2X "
            ,port_num,addr,flags,rw,rw == 1 ? "READ ":"WRITE"
            ,size,                  size == 0 ? "QUICK" :
                                    size == 1 ? "BYTE"  :
                                    size == 2 ? "BYTE_DATA" :
                                    size == 3 ? "WORD_DATA" :
                                    size == 4 ? "PROC_CALL" :
                                    size == 5 ? "BLOCK_DATA" :
                                    size == 8 ? "I2C_BLOCK_DATA" :  "ERROR"
            ,cmd);

    dev_data->rw = rw;
    dev_data->size = size;
    dev_data->data = data;
    dev_data->cmd = cmd;
    dev_data->state = STATE_PROCESS;

    /* Map the size to what the chip understands */
    switch (size) {
        case I2C_SMBUS_QUICK:
        case I2C_SMBUS_BYTE:
        case I2C_SMBUS_BYTE_DATA:
        case I2C_SMBUS_WORD_DATA:
        case I2C_SMBUS_I2C_BLOCK_DATA:
            break;
        default:
            dev_err_ratelimited(&adapter->dev, "Unsupported transaction %d\n", size);
            error = -EOPNOTSUPP;
            return error;
    }

    ////[S][ADDR/R]
    // sent device address with Write mode
    if((addr != I2C_EMUL_SUPPORT_ADD_0) && (addr != I2C_EMUL_SUPPORT_ADD_1)) {
        dev_dbg_ratelimited(&adapter->dev, "Not support addr %x", addr);
        error = -ENXIO;
        return error;
    } else {
        iowrite8(addr, rtc_reg.i2c_addr_reg);
    }

    if (size == I2C_SMBUS_QUICK) {
        return error;
    }

    switch (size) {
        case I2C_SMBUS_BYTE:
        case I2C_SMBUS_BYTE_DATA:
                cnt = 1;  break;
        case I2C_SMBUS_WORD_DATA:
                cnt = 2;  break;
        case I2C_SMBUS_BLOCK_DATA:
        case I2C_SMBUS_I2C_BLOCK_DATA:
        /* In block data modes keep number of byte in block[0] */
                cnt = data->block[0];
                          break;
        default:
                cnt = 0;  break;
    }

    // [DATA]{W}
    mutex_lock(&dev_data->fpga_rtc_lock);
    if( rw == I2C_SMBUS_WRITE && (
            size == I2C_SMBUS_BYTE ||
            size == I2C_SMBUS_BYTE_DATA ||
            size == I2C_SMBUS_WORD_DATA ||
            size == I2C_SMBUS_BLOCK_DATA ||
            size == I2C_SMBUS_I2C_BLOCK_DATA
        )){
        bid = 0;
        dev_dbg_ratelimited(&adapter->dev, "MS prepare to sent [%d bytes]",cnt);
        if(size == I2C_SMBUS_BLOCK_DATA || size == I2C_SMBUS_I2C_BLOCK_DATA){
            bid=1;      // block[0] is cnt;
            cnt+=1;     // offset from block[0]
        }
        for(;bid<cnt;bid++){
            if(size == I2C_SMBUS_BLOCK_DATA || size == I2C_SMBUS_I2C_BLOCK_DATA) {
                iowrite8(data->block[bid], rtc_reg.reg_rtc_data + cmd + bid - 1);
            } else {
                iowrite8(data->block[bid], rtc_reg.reg_rtc_data + cmd + bid);
            }
            dev_dbg_ratelimited(&adapter->dev, "Write  Data > %2.2X",data->block[bid]);
        }
    }

    //// [CMD]{A}
    iowrite8(OPTOE_TIP_BIT, rtc_reg.reg_rtc_sta);
    if (size == I2C_SMBUS_QUICK) {
        cmd = 0;
    }
    if( rw == I2C_SMBUS_WRITE && (
            size == I2C_SMBUS_I2C_BLOCK_DATA
        )){
        ctrl_code = (cmd << I2C_START_BIT) + cnt - 1;
    } else {
        ctrl_code = (cmd << I2C_START_BIT) + cnt;
    }

    dev_dbg_ratelimited(&adapter->dev, "Ctrl code: 0x%x", ctrl_code);
    iowrite16(ctrl_code, rtc_reg.reg_rtc_ctl + 2);
    if(rw == I2C_SMBUS_READ){
        iowrite8(OPTOE_RW_BIT | OPTOE_EN_BIT, rtc_reg.reg_rtc_sta);
    } else {
        iowrite8(OPTOE_EN_BIT, rtc_reg.reg_rtc_sta);
    }
    dev_dbg_ratelimited(&adapter->dev, "MS Start");
    mutex_unlock(&dev_data->fpga_rtc_lock);

    if (!sff_xfer_mode) {
        mutex_lock(&dev_data->fpga_rtc_lock);
        error = rtc_emul_i2c_poll(rw, cmd, size, rtc_reg, data, adapter);
        mutex_unlock(&dev_data->fpga_rtc_lock);
    } else {
        if (wait_event_timeout(dev_data->wait, (dev_data->state == STATE_ERROR) ||
                    (dev_data->state == STATE_DONE), HZ))
            error = (dev_data->state == STATE_DONE) ? 0 : -EIO;
    }
    if (error) {
        dev_dbg_ratelimited(&adapter->dev, "error, port %d , %d\n", port_num, error);
    }

    mutex_lock(&dev_data->fpga_rtc_lock);
    iowrite8(I2C_CLR_REG_VAL, rtc_reg.reg_rtc_sta);
    mutex_unlock(&dev_data->fpga_rtc_lock);

    dev_dbg_ratelimited(&adapter->dev, "Smbus access stop\n");
    return error;
}

static irqreturn_t handle_fpga_rtc_int(int irqno, void *dev_id)
{
   struct daminglake_rtc_emul_i2c_dev_data *dev_data = dev_id;
   emul_rtc_int_process(dev_data);

   return IRQ_HANDLED;
}

static u32 fpga_emul_i2c_func(struct i2c_adapter *a)
{
    return I2C_FUNC_SMBUS_QUICK  |
        I2C_FUNC_SMBUS_BYTE      |
        I2C_FUNC_SMBUS_BYTE_DATA |
        I2C_FUNC_SMBUS_WORD_DATA |
        I2C_FUNC_SMBUS_BLOCK_DATA|
        I2C_FUNC_SMBUS_I2C_BLOCK;
}

static const struct i2c_algorithm fpga_rtc_emul_i2c_algorithm = {
    .smbus_xfer = fpga_emul_i2c_smbus_access,
    .functionality  = fpga_emul_i2c_func,
};

static struct i2c_adapter * emul_i2c_adapter_init(struct platform_device *pdev, struct daminglake_fpga_rtc_priv *priv_data)
{
    int error;
    struct i2c_adapter *new_adapter;
    int portid = priv_data->port_num;
    int irq = priv_data->irq;
    struct daminglake_rtc_emul_i2c_dev_data *daminglake_rtc_emul_i2c_dev_data = priv_data->dev_data;

    new_adapter = kzalloc(sizeof(*new_adapter), GFP_KERNEL);
    if (!new_adapter) {
        dev_err_ratelimited(&pdev->dev, "Cannot alloc emul i2c adapter for I2C_%d\n", portid);
        return NULL;
    }

    daminglake_rtc_emul_i2c_dev_data->base_addr = priv_data->base_addr;
    daminglake_rtc_emul_i2c_dev_data->ram_addr = priv_data->ram_addr;
    daminglake_rtc_emul_i2c_dev_data->portid = portid;
    init_waitqueue_head(&daminglake_rtc_emul_i2c_dev_data->wait);
    mutex_init(&daminglake_rtc_emul_i2c_dev_data->fpga_rtc_lock);

    new_adapter->algo  = &fpga_rtc_emul_i2c_algorithm;
    new_adapter->owner = THIS_MODULE;
    new_adapter->class = I2C_CLASS_DEPRECATED;
    new_adapter->nr = portid + i2c_bus_base;

    new_adapter->algo_data = daminglake_rtc_emul_i2c_dev_data;
    new_adapter->bus_recovery_info = &fpga_rtc_emul_bus_recovery_info;
    snprintf(new_adapter->name, sizeof(new_adapter->name),
        "SMBus Emul Adapter PortID: I2C_%d", portid + i2c_bus_base);

    i2c_set_adapdata(new_adapter, daminglake_rtc_emul_i2c_dev_data);

    if (sff_xfer_mode == XFER_INTERRUPT) {
        if (irq <= 0) {
            kfree_sensitive(new_adapter);
            dev_err_ratelimited(&pdev->dev, "Can not get sff to pci irq!\n");
            return NULL;
        } else {
            error = devm_request_any_context_irq(&pdev->dev, irq, handle_fpga_rtc_int, IRQF_SHARED, new_adapter->name, daminglake_rtc_emul_i2c_dev_data);
            if (error) {
                kfree_sensitive(new_adapter);
                dev_err_ratelimited(&pdev->dev, "Get i2c %d irq req fail! get %d\n", irq, error);
                return NULL;
            }
            dev_dbg_ratelimited(&pdev->dev, "Port %d, get irq: %d\n", portid, irq);
        }
    }

    error = i2c_add_numbered_adapter(new_adapter);
    if(error < 0) {
        dev_err_ratelimited(&pdev->dev, "Cannot add optoe emul adapter %d", portid);
        kfree_sensitive(new_adapter);
        if (sff_xfer_mode == XFER_INTERRUPT) {
            free_irq(irq, daminglake_rtc_emul_i2c_dev_data);
        }
    }

    return new_adapter;
}

static int daminglake_fpga_rtc_drv_probe(struct platform_device *pdev)
{
    struct resource *res[2];
    struct daminglake_fpga_rtc_priv *priv;

    priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    priv->dev_data = devm_kzalloc(&pdev->dev, sizeof(*priv->dev_data), GFP_KERNEL);
    if (!priv->dev_data) {
        return -ENOMEM;
    }

    priv->port_num = pdev->id;

    res[0] = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (unlikely(!res[0])) {
        dev_err_ratelimited(&pdev->dev, "Specified Resource Not Available...\n");
        devm_kfree(&pdev->dev, priv);
        return -1;
    }
    priv->base_addr = devm_ioremap_resource(&pdev->dev, res[0]);
    dev_dbg_ratelimited(&pdev->dev, "Get port id %d, base addr 0x%lx\n", priv->port_num, (unsigned long)priv->base_addr);

    if (sff_xfer_mode == XFER_INTERRUPT) {
        priv->irq = platform_get_irq(pdev, 0);
        dev_dbg_ratelimited(&pdev->dev, "Get port id %d, irq %d\n", priv->port_num, priv->irq);
    } else {
        priv->irq = NONE_IRQ;
    }

    res[1] = platform_get_resource(pdev, IORESOURCE_MEM, 1);
    if (unlikely(!res[1])) {
        devm_kfree(&pdev->dev, priv);
        dev_err_ratelimited(&pdev->dev, "Cannot get fpga rtc device %d ram addr resource!\n", pdev->id);
        return -ENOMEM;
    }
    priv->ram_addr = devm_ioremap_resource(&pdev->dev, res[1]);
    dev_dbg_ratelimited(&pdev->dev, "Get port id %d, ram addr 0x%lx\n", priv->port_num, (unsigned long)priv->ram_addr);

    priv->i2c_adapter = emul_i2c_adapter_init(pdev, priv);
    if (!priv->i2c_adapter) {
        return -ENXIO;
    }

    platform_set_drvdata(pdev, priv);
    dev_info_ratelimited(&pdev->dev, "Port %d rtc emul adapter crated\n", priv->port_num);

    return 0;
}

static int daminglake_fpga_rtc_drv_remove(struct platform_device *pdev)
{
    struct daminglake_fpga_rtc_priv *priv = platform_get_drvdata(pdev);
    int irq = priv->irq;
    void __iomem *ctrl_addr;

    if (sff_xfer_mode == XFER_INTERRUPT) {
        ctrl_addr = priv->base_addr + FPGA_OPTOE1_RTC_STA;
        iowrite8(OPTOE_TIP_BIT, ctrl_addr);

        devm_free_irq(&pdev->dev, irq, priv->dev_data);
    }

    i2c_del_adapter(priv->i2c_adapter);
    devm_iounmap(&pdev->dev, priv->base_addr);
    devm_iounmap(&pdev->dev, priv->ram_addr);
    devm_kfree(&pdev->dev, priv);
    dev_info_ratelimited(&pdev->dev, "Port %d rtc emul adapter remove\n", priv->port_num);

    return 0;
}

static const struct platform_device_id daminglake_fpga_rtc_driver_id[] = {
    {.name = "fpga_rtc_dev",},
    {}
};

static struct platform_driver daminglake_fpga_rtc_drv = {
        .probe  = daminglake_fpga_rtc_drv_probe,
        .remove = daminglake_fpga_rtc_drv_remove,
        .driver = {
                .name   = "daminglake_fpga_rtc_driver",
        },
    .id_table = daminglake_fpga_rtc_driver_id,
};

module_platform_driver(daminglake_fpga_rtc_drv);

MODULE_AUTHOR("baotuspringswitch baotuspringswitch@gmail.com");
MODULE_DESCRIPTION("Daminglake FPGA optoe RTC emul i2c adapter driver");
MODULE_LICENSE("GPL");



