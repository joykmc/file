/*
 * fpga_mdio.c - driver for Daminglake FPGA PCIE to MDIO function.
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
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <uapi/linux/stat.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/moduleparam.h>
#include <linux/iopoll.h>
#include <linux/completion.h>
#include <linux/phy.h>

#include "daminglake_fpga_mdio.h"

static int mdio_xfer_mode = MDIO_XFER_POLLING;
module_param(mdio_xfer_mode, int, 0644);
MODULE_PARM_DESC(mdio_xfer_mode,
        "FPGA mdio transfer type(0:polling, 1:interrupt)");

static int fpga_mdio_open(struct inode *inode, struct file *file)
{
    struct daminglake_fpga_mdio_device *mdio_device = container_of(inode->i_cdev,
                 struct daminglake_fpga_mdio_device, cdev);
    
    file->private_data = mdio_device;
    return 0;
}

static long fpga_mdio_unlocked_ioctl(struct file *file, 
                     unsigned int cmd, unsigned long arg)
{
    struct fpga_mdio_ioctl_data mii_data;    
    struct daminglake_fpga_mdio_device *mdio_device = file->private_data;
    struct phy_device *phydev;
    int ret;

    if (copy_from_user(&mii_data, (void __user*)arg,
               sizeof(mii_data)) != 0) 
        return -EFAULT;

    phydev = mdio_device->fpga_mdio_data.phydev;
    if (!phydev) 
        return -ENODEV;    
    
    switch (cmd) {
    case SIOCGMIIREG:
        ret = mdiobus_read(phydev->mdio.bus,
                   mii_data.phy_addr, mii_data.reg_num);
        if (ret < 0) 
            return ret;

        mii_data.val_out = ret;
        if (copy_to_user((void __user*)arg ,&mii_data,
                 sizeof(mii_data)) != 0) 
            return -EFAULT;
                
        return 0;

    case SIOCSMIIREG:
        ret = mdiobus_write(phydev->mdio.bus, mii_data.phy_addr,
                    mii_data.reg_num, mii_data.val_in);
        
        return ret;

    default:
        return -EOPNOTSUPP;
    }

}

const struct file_operations fpga_mdio_fops = {
    .owner = THIS_MODULE,
    .open = fpga_mdio_open,
    .unlocked_ioctl = fpga_mdio_unlocked_ioctl,
};

static int fpga_mdio_bus_write_polling_transfer(struct mii_bus *bus,
                        int mii_id, int reg, u16 value)
{
    u32 cmd = 0;
    u32 cfg = 0;
    int ret, tmp = 0;
    struct daminglake_fpga_mdio_priv *priv = bus->priv;

    cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
    writel(cfg, &priv->mdio_reg->STA_REG3);
    
    /* Write CFG_REG1 */
    cmd = FPGA_MDIO_CFG_TYPE(MDIO_CL22_ENABLE) | FPGA_MDIO_CFG_CL22PRE_EN(MDIO_CL22_PRE_ENABLE) |
          FPGA_MDIO_CMD_PHYAD(mii_id) | FPGA_MDIO_CMD_DEVTYPE(DEVICETYPE_PMA_PMD);
    writel(cmd, &priv->mdio_reg->CFG_REG0);

    /* Start to write PHY Reg */
    writel(reg & 0xffff, &priv->mdio_reg->ADR_REG1);
    writel(value, &priv->mdio_reg->RAW_REG2);

    cfg = FPGA_MDIO_STA_EN(1) | FPGA_MDIO_STA_OP(0) | FPGA_MDIO_STA_TIP(0) |
          FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
    writel(cfg, &priv->mdio_reg->STA_REG3);

    udelay(2);

    ret = readl_poll_timeout_atomic(&priv->mdio_reg->STA_REG3, 
                    tmp, (tmp & FPGA_MDIO_STA_TIP(1)), 2, 100);
    if(ret < 0) {
        dev_err_ratelimited(&bus->dev, "mdio write bus %s reg 0x%x value 0x%x timeout.\n", 
            bus->id, reg, value);
        cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
        writel(cfg, &priv->mdio_reg->STA_REG3);

        return ret;
    }
    udelay(2);
    cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
    writel(cfg, &priv->mdio_reg->STA_REG3);

    return 0;
}

static int fpga_mdio_bus_read_polling_transfer(struct mii_bus *bus,
                           int mii_id, int reg)
{
    u32 cmd = 0;
    u32 cfg = 0;
    int value, tmp, ret = 0;
    struct daminglake_fpga_mdio_priv *priv = bus->priv;

    cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
    writel(cfg, &priv->mdio_reg->STA_REG3);
    
    /* Write CFG_REG1 */
    cmd = FPGA_MDIO_CFG_TYPE(MDIO_CL22_ENABLE) | FPGA_MDIO_CFG_CL22PRE_EN(MDIO_CL22_PRE_ENABLE) |
          FPGA_MDIO_CMD_PHYAD(mii_id) | FPGA_MDIO_CMD_DEVTYPE(DEVICETYPE_PMA_PMD);
    writel(cmd, &priv->mdio_reg->CFG_REG0);

    /* Start to write PHY Reg */
    writel(reg & 0xffff, &priv->mdio_reg->ADR_REG1);

    cfg = FPGA_MDIO_STA_EN(1) | FPGA_MDIO_STA_OP(1) | FPGA_MDIO_STA_TIP(0) |
          FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
    writel(cfg, &priv->mdio_reg->STA_REG3);

    udelay(2);
    ret = readl_poll_timeout_atomic(&priv->mdio_reg->STA_REG3, 
                    tmp, (tmp & FPGA_MDIO_STA_TIP(1)), 2, 10000);

    if(ret < 0) {
        dev_err_ratelimited(&bus->dev, "mdio read bus %s reg 0x%x timeout.\n", 
            bus->id, reg);
        cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
        writel(cfg, &priv->mdio_reg->STA_REG3);

        return ret;
    }
    udelay(2);

    cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
    writel(cfg, &priv->mdio_reg->STA_REG3);

    value = readl(&priv->mdio_reg->RAW_REG2) >> 16; 
    dev_dbg_ratelimited(&bus->dev, "mdio read bus %s reg 0x%x value 0x%x.\n", 
        bus->id, reg, value);

    udelay(2);

    return value & 0xffff;
}

static irqreturn_t fpga_mdio_irq_isr(int irq, void *dev_id)
{
	struct daminglake_fpga_mdio_priv *mii_priv = dev_id;
	complete(&mii_priv->cmd_complete);

	return IRQ_HANDLED;
}

static inline int fpga_mdio_check_tx_over(struct daminglake_fpga_mdio_priv *priv)
{
	int value;

	if (!wait_for_completion_timeout(&priv->cmd_complete, HZ)) {
		dev_err_ratelimited(&priv->pdev->dev, "FPGA msi interrupt timeout.\n");
		return -ETIMEDOUT;
	}

	value = (readl(&priv->mdio_reg->STA_REG3) & 0xffff);
	if (!(value & FPGA_MDIO_STA_INTERRUPT_FLAG(1)))
		return -EIO;

	if (value & FPGA_MDIO_STA_TIP(1))
		return -EIO;

	return 0;
}

static inline int fpga_mdio_clear_interrupt_flag(struct daminglake_fpga_mdio_priv *priv)
{
	int value;

	value = (readl(&priv->mdio_reg->STA_REG3) & 0xffff);
	value |= FPGA_MDIO_STA_IACK(1);
	writel(value, &priv->mdio_reg->STA_REG3);
	value &= ~(FPGA_MDIO_STA_IACK(1));
	writel(value, &priv->mdio_reg->STA_REG3);

	value = (readl(&priv->mdio_reg->STA_REG3) & 0xffff);
	if (value & FPGA_MDIO_STA_INTERRUPT_FLAG(1))
		return -EIO;

	if (value & FPGA_MDIO_STA_TIP(1))
		return -EIO;

	return 0;

}

static int fpga_mdio_bus_write_interrupt_transfer(struct mii_bus *bus,
                        int mii_id, int reg, u16 value)
{
    u32 cmd = 0;
    u32 cfg = 0;
    int ret = 0;
    struct daminglake_fpga_mdio_priv *priv = bus->priv;

    reinit_completion(&priv->cmd_complete);
    cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
    writel(cfg, &priv->mdio_reg->STA_REG3);
    
    /* Write CFG_REG1 */
    cmd = FPGA_MDIO_CFG_TYPE(MDIO_CL22_ENABLE) | FPGA_MDIO_CFG_CL22PRE_EN(MDIO_CL22_PRE_ENABLE) |
          FPGA_MDIO_CMD_PHYAD(mii_id) | FPGA_MDIO_CMD_DEVTYPE(DEVICETYPE_PMA_PMD);
    writel(cmd, &priv->mdio_reg->CFG_REG0);

    /* Start to write PHY Reg */
    writel(reg & 0xffff, &priv->mdio_reg->ADR_REG1);
    writel(value, &priv->mdio_reg->RAW_REG2);

    cfg = FPGA_MDIO_STA_EN(1) | FPGA_MDIO_STA_OP(0) | FPGA_MDIO_STA_TIP(0) |
          FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
    writel(cfg, &priv->mdio_reg->STA_REG3);

    udelay(2);

    /* Wait for mdio frame to complete */
	ret = fpga_mdio_check_tx_over(priv);
	if (ret < 0) {
		dev_err_ratelimited(&bus->dev, "mdio write bus %s reg 0x%x interrupt timed out.\n",
			 bus->id, reg);
		cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
        writel(cfg, &priv->mdio_reg->STA_REG3);
		return ret;
	}

	ret = fpga_mdio_clear_interrupt_flag(priv);
	if (ret < 0) {
		dev_err_ratelimited(&bus->dev, "mdio write bus %s reg 0x%x clear interrupt failed.\n",
			 bus->id, reg);
		cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
        writel(cfg, &priv->mdio_reg->STA_REG3);
		return ret;
	}

    udelay(2);
    cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
    writel(cfg, &priv->mdio_reg->STA_REG3);

    return 0;
}

static int fpga_mdio_bus_read_interrupt_transfer(struct mii_bus *bus,
                           int mii_id, int reg)
{
    u32 cmd = 0;
    u32 cfg = 0;
    int value, ret = 0;
    struct daminglake_fpga_mdio_priv *priv = bus->priv;

    reinit_completion(&priv->cmd_complete);
    cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
    writel(cfg, &priv->mdio_reg->STA_REG3);
    
    /* Write CFG_REG1 */
    cmd = FPGA_MDIO_CFG_TYPE(MDIO_CL22_ENABLE) | FPGA_MDIO_CFG_CL22PRE_EN(MDIO_CL22_PRE_ENABLE) |
          FPGA_MDIO_CMD_PHYAD(mii_id) | FPGA_MDIO_CMD_DEVTYPE(DEVICETYPE_PMA_PMD);
    writel(cmd, &priv->mdio_reg->CFG_REG0);

    /* Start to write PHY Reg */
    writel(reg & 0xffff, &priv->mdio_reg->ADR_REG1);

    cfg = FPGA_MDIO_STA_EN(1) | FPGA_MDIO_STA_OP(1) | FPGA_MDIO_STA_TIP(0) |
          FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
    writel(cfg, &priv->mdio_reg->STA_REG3);

    udelay(2);
    /* Wait for mdio frame to complete */
	ret = fpga_mdio_check_tx_over(priv);
	if (ret < 0) {
		dev_err_ratelimited(&bus->dev, "mdio write bus %s reg 0x%x interrupt timed out.\n",
			 bus->id, reg);
		cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
        writel(cfg, &priv->mdio_reg->STA_REG3);
		return ret;
	}

	ret = fpga_mdio_clear_interrupt_flag(priv);
	if (ret < 0) {
		dev_err_ratelimited(&bus->dev, "mdio write bus %s reg 0x%x clear interrupt failed.\n",
			 bus->id, reg);
		cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
        writel(cfg, &priv->mdio_reg->STA_REG3);
		return ret;
	}
    udelay(2);

    cfg = FPGA_MDIO_STA_EN(0) | FPGA_MDIO_STA_TIP(1) | FPGA_MDIO_STA_FREQ(FPGA_MDIO_CLK_1M);
    writel(cfg, &priv->mdio_reg->STA_REG3);

    value = readl(&priv->mdio_reg->RAW_REG2) >> 16; 
    dev_dbg_ratelimited(&bus->dev, "mdio read bus %s reg 0x%x value 0x%x.\n", 
        bus->id, reg, value);

    udelay(2);

    return value & 0xffff;
}

static int fpga_mdio_bus_write(struct mii_bus *bus, int mii_id, int reg,
                   u16 value)
{
    if (mdio_xfer_mode == MDIO_XFER_INTERRUPT)
		return fpga_mdio_bus_write_interrupt_transfer(bus, mii_id, reg,
							      value);
    
    return fpga_mdio_bus_write_polling_transfer(bus, mii_id, reg, value);
}

static int fpga_mdio_bus_read(struct mii_bus *bus, int mii_id, int reg)
{
    if (mdio_xfer_mode == MDIO_XFER_INTERRUPT)
		return fpga_mdio_bus_read_interrupt_transfer(bus, mii_id, reg);
    
    return fpga_mdio_bus_read_polling_transfer(bus, mii_id, reg);
}

static int switchboard_netdev_init(struct platform_device *pdev, int phy_num)
{
    struct daminglake_fpga_mdio_priv *mii_priv;    
    struct mii_bus *mii_bus;
    struct phy_device *phydev;
    struct daminglake_fpga_mdio_device *mdio_device = dev_get_drvdata(&pdev->dev);
    //struct phy_c45_device_ids c45_ids = {0};
    int err;
    int irq = mdio_device->mdio_irq + MDIO_IRQ_BASE + pdev->id;

    mii_bus = mdiobus_alloc_size(sizeof(*mii_priv));
    if (!mii_bus) 
        return -ENOMEM;
    
    mii_bus->name = "fpga_lc_mdio";
    snprintf(mii_bus->id, MII_BUS_ID_SIZE, "%s_LC_%d", 
         "fpga_mii", phy_num);
    
    mii_bus->read = fpga_mdio_bus_read;
    mii_bus->write = fpga_mdio_bus_write;
    mii_priv = mii_bus->priv;
    mii_priv->pdev = pdev; 
    mii_priv->map = mdio_device->phy_base_addr;
    mii_priv->mdio_reg = (struct MdioFpga_regs *)mii_priv->map;
    mdio_device->fpga_mdio_data.mii_bus = mii_bus;

    /* Initialize PHY device clock to 1M */
    writel(FPGA_MDIO_CLK_1M, &mii_priv->mdio_reg->STA_REG3);
    
    /* Initialize credo phy device to push-pull mode 
    */
    if (mdio_xfer_mode == MDIO_XFER_INTERRUPT) {
		init_completion(&mii_priv->cmd_complete);
		err = devm_request_irq(&pdev->dev, irq,
				       fpga_mdio_irq_isr, IRQF_EARLY_RESUME, mii_bus->id,
				       mii_priv);
		if (err) {
			dev_err_ratelimited(&pdev->dev, "cannot request mdio irq.\n");
			goto free_mdiobus;
		}
		dev_info_ratelimited(&pdev->dev, "%s request irq num is %d.\n", mii_bus->id, irq);
	}

    /* Register FPGA_LC mdio bus */
    err = mdiobus_register(mii_bus);
    if (err) {
        dev_err_ratelimited(&mii_bus->dev, "cannot register mdio bus.\n");
        goto free_irq;
    }

    phydev = phy_find_first(mii_bus);
    if (!phydev) {
        dev_err_ratelimited(&mii_bus->dev, "no phy device found.\n");
        return 0;
    }
    mdio_device->fpga_mdio_data.phydev = phydev;
    /* rtl l2 switch do not response traditional mdio access, so create phy device force */
    
    return 0;

free_irq:
	if (mdio_xfer_mode == MDIO_XFER_INTERRUPT) {
		devm_free_irq(&pdev->dev, irq, mii_priv);
		writel(FPGA_MDIO_CLK_1M, &mii_priv->mdio_reg->STA_REG3);
	}
free_mdiobus:
    mdiobus_free(mii_bus);
    return err;
}

static int daminglake_fpga_mdio_driver_probe(struct platform_device *pdev)
{
        struct daminglake_fpga_mdio_device *mdio_device;
        int ret;
        dev_t devno = 0;
        char *class_name;
        struct resource *res[2];

        class_name = "fpga-mdio-dev";

        mdio_device = devm_kzalloc(&pdev->dev, sizeof(*mdio_device), GFP_KERNEL);
        if (!mdio_device) {
            ret = -ENOMEM;
            return ret;
        }
        
        res[0] = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        if (unlikely(!res[0])) {
            devm_kfree(&pdev->dev, mdio_device);
            dev_err_ratelimited(&pdev->dev, "Cannot get ocores device %d resource!\n", pdev->id);
            return -ENOMEM;
        }
        mdio_device->phy_base_addr = devm_ioremap_resource(&pdev->dev, res[0]);
        if (IS_ERR(mdio_device->phy_base_addr)) {
            devm_kfree(&pdev->dev, mdio_device);
            dev_err_ratelimited(&pdev->dev, "Cannot remap ocores device %d resource, get 0x%lx!\n", pdev->id, (unsigned long)mdio_device->phy_base_addr);
            return -ENOMEM;
        }
        dev_info_ratelimited(&pdev->dev, "FPGA_MDIO device get addr 0x%lx\n", (unsigned long)mdio_device->phy_base_addr);

        res[1] = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
        if (unlikely(!res[1])) {
            devm_kfree(&pdev->dev, mdio_device);
            dev_err_ratelimited(&pdev->dev, "Cannot get ocores device %d resource!\n", pdev->id);
            return -ENOMEM;
        }
        mdio_device->mdio_irq = res[1]->start;

        ret = alloc_chrdev_region(&devno, 0, 1, "fpga-mdio");
        if (ret < 0) {
            dev_err_ratelimited(&pdev->dev, "failed to alloc chrdev region.\n");
            return ret;
        }

        mdio_device->mdio_major = MAJOR(devno);    
        cdev_init(&mdio_device->cdev, &fpga_mdio_fops);
        mdio_device->cdev.owner = THIS_MODULE;
        ret = cdev_add(&mdio_device->cdev, devno, 1);
        if (ret) {
            dev_err_ratelimited(&pdev->dev, "Failed to add LC cdev!\n");
            return ret;
        }

        mdio_device->fpga_mdio_class = class_create(THIS_MODULE, class_name);
        if (IS_ERR(mdio_device->fpga_mdio_class)) {        
            dev_err_ratelimited(&pdev->dev, "Failed to create LC mdio class.\n");
            ret = PTR_ERR(mdio_device->fpga_mdio_class);   
            goto cdev_del;
        }

        mdio_device->fpga_mdio_dev= device_create(mdio_device->fpga_mdio_class,
                            NULL, 
                            MKDEV(mdio_device->mdio_major, pdev->id),
                            NULL, "mdio-%d", pdev->id);
        if (IS_ERR(mdio_device->fpga_mdio_dev)) {
            dev_err_ratelimited(&pdev->dev, "Failed to create LC mdio dev.\n");
            ret = PTR_ERR(mdio_device->fpga_mdio_dev);
            goto class_destroy;
        }

        platform_set_drvdata(pdev, mdio_device);
        
        ret = switchboard_netdev_init(pdev, pdev->id);
        if (ret < 0) {
            dev_err_ratelimited(&pdev->dev, "Failed to create LC mdio bus 0.\n");
            mdiobus_free(mdio_device->fpga_mdio_data.mii_bus);
            goto device_destroy;
        }
        return 0;
        
    device_destroy:
        device_destroy(mdio_device->fpga_mdio_class, 
                MKDEV(mdio_device->mdio_major, 0));
        
    class_destroy:
        class_destroy(mdio_device->fpga_mdio_class);

    cdev_del:
        cdev_del(&mdio_device->cdev);
        unregister_chrdev_region(devno, 1);

        return ret;
}

static int daminglake_fpga_mdio_driver_remove(struct platform_device *pdev)
{
        struct daminglake_fpga_mdio_device *mdio_device = dev_get_drvdata(&pdev->dev);
        struct daminglake_fpga_mdio_priv *priv = mdio_device->fpga_mdio_data.mii_bus->priv;
        int irq = mdio_device->mdio_irq + MDIO_IRQ_BASE + pdev->id;

        if (mdio_xfer_mode == MDIO_XFER_INTERRUPT) {
		    devm_free_irq(&pdev->dev, irq, priv);
		    writel(FPGA_MDIO_CLK_1M, &priv->mdio_reg->STA_REG3);
	    }

        device_destroy(mdio_device->fpga_mdio_class, 
                MKDEV(mdio_device->mdio_major, pdev->id));
        class_destroy(mdio_device->fpga_mdio_class);
        cdev_del(&mdio_device->cdev);
        unregister_chrdev_region(MKDEV(mdio_device->mdio_major, 0), 1);

        mdiobus_unregister(mdio_device->fpga_mdio_data.mii_bus);
        mdiobus_free(mdio_device->fpga_mdio_data.mii_bus);

        return 0;
}

static const struct platform_device_id daminglake_fpga_mdio_devid[] ={
    {.name = "fpga_mdio_dev"},
    {},
};
MODULE_DEVICE_TABLE(platform, daminglake_fpga_mdio_devid);


static struct platform_driver daminglake_fpga_mdio_driver = {
    .driver = {
        .name = "daminglake_fpga_mdio_driver",
    },
    .probe = daminglake_fpga_mdio_driver_probe,
    .remove = daminglake_fpga_mdio_driver_remove,
    .id_table	= daminglake_fpga_mdio_devid,
};

module_platform_driver(daminglake_fpga_mdio_driver);

MODULE_AUTHOR("baotuspringswitch baotuspringswitch@gmail.com");
MODULE_DESCRIPTION("Daminglake fpga mdio driver");
MODULE_LICENSE("GPL");

