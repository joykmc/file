// SPDX-License-Identifier: GPL-2.0
/*
 * i2c-ocores.c: I2C bus driver for OpenCores I2C controller
 * (https://opencores.org/project/i2c/overview)
 *
 * baotuspringswitch baotuspringswitch@gmail.com
 *
 * Support for the GRLIB port of the controller by
 * Andreas Larsson <andreas@gaisler.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/platform_data/i2c-ocores.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>

#include "daminglake_fpga_ocores.h"

static int ocores_xfer_mode = XFER_INTERRUPT;
module_param(ocores_xfer_mode, int, 0644);
MODULE_PARM_DESC(ocores_xfer_mode,
        "FPGA ocore transfer type(0:polling, 1:interrupt)");

static int reg_io_width = 1;
module_param(reg_io_width, int, 0644);
MODULE_PARM_DESC(reg_io_width,
        "FPGA ocores register access length.");

static int i2c_clock_khz = 100;
module_param(i2c_clock_khz, int, 0644);
MODULE_PARM_DESC(i2c_clock_khz,
        "FPGA ocores i2c clock (kHz).");

static int i2c_bus_base = 0;
module_param(i2c_bus_base, int, 0644);
MODULE_PARM_DESC(i2c_bus_base,
        "FPGA ocores i2c adapter base number.");

static int i2c_retry_times = 3;
module_param(i2c_retry_times, int, 0644);
MODULE_PARM_DESC(i2c_retry_times,
        "FPGA i2c adapter retry times.");

static int i2c_retry_delay = 1;
module_param(i2c_retry_delay, int, 0644);
MODULE_PARM_DESC(i2c_retry_delay,
        "FPGA i2c adapter retry time delay, by 1s.");

static struct i2c_adapter ocores_adapter;

static ssize_t show_i2c_bus_hang_sta(struct device *dev, struct device_attribute *attr, char *buf)
{
        /*To do: Add i2c bus hang sysfs node*/
        return -1;
}

static ssize_t store_i2c_bus_hang_sta(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
        /*To do: Add i2c bus hang sysfs node*/
        return count;
}

static ssize_t show_i2c_bus_reset(struct device *dev, struct device_attribute *attr, char *buf)
{
        /*To do: Add i2c bus reset sysfs node*/
        return -1;
}

static ssize_t store_i2c_bus_reset(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
        /*To do: Add i2c bus reset sysfs node*/
        return count;
}

static DEVICE_ATTR(i2c_bus_hang_sta, 0664, show_i2c_bus_hang_sta, store_i2c_bus_hang_sta);
static DEVICE_ATTR(i2c_bus_reset, 0664, show_i2c_bus_reset, store_i2c_bus_reset);

static struct attribute *i2c_ocores_attrs[] = {
    &dev_attr_i2c_bus_hang_sta.attr,
	&dev_attr_i2c_bus_reset.attr,
    NULL,
};

static struct attribute_group i2c_ocores_attr_grp = {
        .attrs = i2c_ocores_attrs,
};

static int i2c_ocores_recover_bus(struct i2c_adapter *adap) {
    //To do: Add i2c bus auto clear function
    return -1;
}

static int i2c_ocores_bus_reset(struct i2c_adapter *adap) {
    //To do: Add i2c bus auto reeset function
    return -1;
}

static int i2c_ocores_get_scl(struct i2c_adapter *adap) {
    //To do: Add i2c bus auto clear function
    return -1;
}

static void i2c_ocores_set_scl(struct i2c_adapter *adap, int val) {
    //To do: Add i2c bus auto clear function
}

static int i2c_ocores_get_sda(struct i2c_adapter *adap) {
    //To do: Add i2c bus auto clear function
    return -1;
}

static struct i2c_bus_recovery_info i2c_ocores_bus_recovery_info = {
	.get_scl		= i2c_ocores_get_scl,
	.get_sda		= i2c_ocores_get_sda,
	.set_scl		= i2c_ocores_set_scl,
	.recover_bus		= i2c_ocores_recover_bus,
};

static void oc_setreg_8(struct ocores_i2c *i2c, int reg, u8 value)
{
	iowrite8(value, i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_16(struct ocores_i2c *i2c, int reg, u8 value)
{
	iowrite16(value, i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_32(struct ocores_i2c *i2c, int reg, u8 value)
{
	iowrite32(value, i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_16be(struct ocores_i2c *i2c, int reg, u8 value)
{
	iowrite16be(value, i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_32be(struct ocores_i2c *i2c, int reg, u8 value)
{
	iowrite32be(value, i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_8(struct ocores_i2c *i2c, int reg)
{
	return ioread8(i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_16(struct ocores_i2c *i2c, int reg)
{
	return ioread16(i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_32(struct ocores_i2c *i2c, int reg)
{
	return ioread32(i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_16be(struct ocores_i2c *i2c, int reg)
{
	return ioread16be(i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_32be(struct ocores_i2c *i2c, int reg)
{
	return ioread32be(i2c->base + (reg << i2c->reg_shift));
}

static inline void oc_setreg(struct ocores_i2c *i2c, int reg, u8 value)
{
	i2c->setreg(i2c, reg, value);
}

static inline u8 oc_getreg(struct ocores_i2c *i2c, int reg)
{
	return i2c->getreg(i2c, reg);
}

static void ocores_process(struct ocores_i2c *i2c, u8 stat)
{
	struct i2c_msg *msg = i2c->msg;
	unsigned long flags;

	/*
	 * If we spin here is because we are in timeout, so we are going
	 * to be in STATE_ERROR. See ocores_process_timeout()
	 */
	spin_lock_irqsave(&i2c->process_lock, flags);

	if ((i2c->state == STATE_DONE) || (i2c->state == STATE_ERROR)) {
		/* stop has been sent */
		oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);
		wake_up(&i2c->wait);
		goto out;
	}

	/* error? */
	if (stat & OCI2C_STAT_ARBLOST) {
		i2c->state = STATE_ERROR;
		oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
		goto out;
	}

	if ((i2c->state == STATE_START) || (i2c->state == STATE_WRITE)) {
		i2c->state =
			(msg->flags & I2C_M_RD) ? STATE_READ : STATE_WRITE;

		if (stat & OCI2C_STAT_NACK) {
			i2c->state = STATE_ERROR;
			oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
			goto out;
		}
	} else {
		msg->buf[i2c->pos++] = oc_getreg(i2c, OCI2C_DATA);
	}

	/* end of msg? */
	if (i2c->pos == msg->len) {
		i2c->nmsgs--;
		i2c->msg++;
		i2c->pos = 0;
		msg = i2c->msg;

		if (i2c->nmsgs) {	/* end? */
			/* send start? */
			if (!(msg->flags & I2C_M_NOSTART)) {
				u8 addr = i2c_8bit_addr_from_msg(msg);

				i2c->state = STATE_START;

				oc_setreg(i2c, OCI2C_DATA, addr);
				oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_START);
				goto out;
			}
			i2c->state = (msg->flags & I2C_M_RD)
				? STATE_READ : STATE_WRITE;
		} else {
			i2c->state = STATE_DONE;
			oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
			goto out;
		}
	}

	if (i2c->state == STATE_READ) {
		oc_setreg(i2c, OCI2C_CMD, i2c->pos == (msg->len-1) ?
			  OCI2C_CMD_READ_NACK : OCI2C_CMD_READ_ACK);
	} else {
		oc_setreg(i2c, OCI2C_DATA, msg->buf[i2c->pos++]);
		oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_WRITE);
	}

out:
	spin_unlock_irqrestore(&i2c->process_lock, flags);
}

static irqreturn_t ocores_isr(int irq, void *dev_id)
{
	struct ocores_i2c *i2c = dev_id;
	u8 stat = oc_getreg(i2c, OCI2C_STATUS);

    if (!(stat & OCI2C_STAT_IF)) {
		return IRQ_NONE;
	}
	ocores_process(i2c, stat);

	return IRQ_HANDLED;
}

/**
 * Process timeout event
 * @i2c: ocores I2C device instance
 */
static void ocores_process_timeout(struct ocores_i2c *i2c)
{
	unsigned long flags;

	spin_lock_irqsave(&i2c->process_lock, flags);
	i2c->state = STATE_ERROR;
	oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
	spin_unlock_irqrestore(&i2c->process_lock, flags);
}

/**
 * Wait until something change in a given register
 * @i2c: ocores I2C device instance
 * @reg: register to query
 * @mask: bitmask to apply on register value
 * @val: expected result
 * @timeout: timeout in jiffies
 *
 * Timeout is necessary to avoid to stay here forever when the chip
 * does not answer correctly.
 *
 * Return: 0 on success, -ETIMEDOUT on timeout
 */
static int ocores_wait(struct ocores_i2c *i2c,
		       int reg, u8 mask, u8 val,
		       const unsigned long timeout)
{
	unsigned long j;

	j = jiffies + timeout;
	while (1) {
		u8 status = oc_getreg(i2c, reg);

		if ((status & mask) == val)
			break;

		if (time_after(jiffies, j))
			return -ETIMEDOUT;
	}
	return 0;
}

/**
 * Wait until is possible to process some data
 * @i2c: ocores I2C device instance
 *
 * Used when the device is in polling mode (interrupts disabled).
 *
 * Return: 0 on success, -ETIMEDOUT on timeout
 */
static int ocores_poll_wait(struct ocores_i2c *i2c)
{
	u8 mask;
	int err;

	if (i2c->state == STATE_DONE || i2c->state == STATE_ERROR) {
		/* transfer is over */
		mask = OCI2C_STAT_BUSY;
	} else {
		/* on going transfer */
		mask = OCI2C_STAT_TIP;
		/*
		 * We wait for the data to be transferred (8bit),
		 * then we start polling on the ACK/NACK bit
		 */
		udelay((8 * 1000) / i2c->bus_clock_khz);
	}

	/*
	 * once we are here we expect to get the expected result immediately
	 * so if after 1ms we timeout then something is broken.
	 */
	err = ocores_wait(i2c, OCI2C_STATUS, mask, 0, msecs_to_jiffies(1));
	if (err)
		dev_warn(i2c->adap.dev.parent,
			 "%s: STATUS timeout, bit 0x%x did not clear in 1ms\n",
			 __func__, mask);
	return err;
}

/**
 * It handles an IRQ-less transfer
 * @i2c: ocores I2C device instance
 *
 * Even if IRQ are disabled, the I2C OpenCore IP behavior is exactly the same
 * (only that IRQ are not produced). This means that we can re-use entirely
 * ocores_isr(), we just add our polling code around it.
 *
 * It can run in atomic context
 *
 * Return: 0 on success, -ETIMEDOUT on timeout
 */
static int ocores_process_polling(struct ocores_i2c *i2c)
{
	irqreturn_t ret;
	int err = 0;

	while (1) {
		err = ocores_poll_wait(i2c);
		if (err)
			break; /* timeout */

		ret = ocores_isr(-1, i2c);
		if (ret == IRQ_NONE)
			break; /* all messages have been transferred */
		else {
			if (i2c->state == STATE_DONE)
				break;
		}
	}

	return err;
}

static int ocores_xfer_core(struct ocores_i2c *i2c,
			    struct i2c_msg *msgs, int num,
			    bool polling)
{
	int ret = 0;
	u8 ctrl;

	ctrl = oc_getreg(i2c, OCI2C_CONTROL);
	if (polling)
		oc_setreg(i2c, OCI2C_CONTROL, ctrl & ~OCI2C_CTRL_IEN);
	else
		oc_setreg(i2c, OCI2C_CONTROL, ctrl | OCI2C_CTRL_IEN);

	i2c->msg = msgs;
	i2c->pos = 0;
	i2c->nmsgs = num;
	i2c->state = STATE_START;

	oc_setreg(i2c, OCI2C_DATA, i2c_8bit_addr_from_msg(i2c->msg));
	oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_START);

	if (polling) {
		ret = ocores_process_polling(i2c);
	} else {
		if (wait_event_timeout(i2c->wait,
				       (i2c->state == STATE_ERROR) ||
				       (i2c->state == STATE_DONE), HZ) == 0)
			ret = -ETIMEDOUT;
	}
	if (ret) {
		ocores_process_timeout(i2c);
		return ret;
	}

	return (i2c->state == STATE_DONE) ? num : -EIO;
}

static int ocores_xfer_polling(struct i2c_adapter *adap,
			       struct i2c_msg *msgs, int num)
{
	return ocores_xfer_core(i2c_get_adapdata(adap), msgs, num, true);
}

static int ocores_xfer(struct i2c_adapter *adap,
		       struct i2c_msg *msgs, int num)
{
	return ocores_xfer_core(i2c_get_adapdata(adap), msgs, num, false);
}

static int ocores_init(struct device *dev, struct ocores_i2c *i2c)
{
	int prescale;
	int diff;
	u8 ctrl = oc_getreg(i2c, OCI2C_CONTROL);

	/* make sure the device is disabled */
	ctrl &= ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN);
	oc_setreg(i2c, OCI2C_CONTROL, ctrl);

	prescale = (i2c->ip_clock_khz / (5 * i2c->bus_clock_khz)) - 1;
	prescale = clamp(prescale, 0, 0xffff);

	diff = i2c->ip_clock_khz / (5 * (prescale + 1)) - i2c->bus_clock_khz;
	if (abs(diff) > i2c->bus_clock_khz / 10) {
		dev_err_ratelimited(dev,
			"Unsupported clock settings: core: %d KHz, bus: %d KHz\n",
			i2c->ip_clock_khz, i2c->bus_clock_khz);
		return -EINVAL;
	}

	oc_setreg(i2c, OCI2C_PRELOW, prescale & 0xff);
	oc_setreg(i2c, OCI2C_PREHIGH, prescale >> 8);

	/* Init the device */
	oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);
	oc_setreg(i2c, OCI2C_CONTROL, ctrl | OCI2C_CTRL_EN);

	return 0;
}


static u32 ocores_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm ocores_algorithm = {
	.master_xfer = ocores_xfer,
	.master_xfer_atomic = ocores_xfer_polling,
	.functionality = ocores_func,
};

static int ocores_i2c_probe(struct platform_device *pdev)
{
	struct ocores_i2c *i2c;
	struct resource *res;
	int irq;
	int ret;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	spin_lock_init(&i2c->process_lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		i2c->base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(i2c->base))
			return PTR_ERR(i2c->base);
	}

    i2c->reg_io_width = reg_io_width;
	i2c->ip_clock_khz = 62500;
	i2c->bus_clock_khz = i2c_clock_khz;
    i2c->reg_shift = 2;

	if (i2c->reg_io_width == 0)
		i2c->reg_io_width = 1; /* Set to default value */

	if (!i2c->setreg || !i2c->getreg) {
		bool be = 0;

		switch (i2c->reg_io_width) {
		case 1:
			i2c->setreg = oc_setreg_8;
			i2c->getreg = oc_getreg_8;
			break;

		case 2:
			i2c->setreg = be ? oc_setreg_16be : oc_setreg_16;
			i2c->getreg = be ? oc_getreg_16be : oc_getreg_16;
			break;

		case 4:
			i2c->setreg = be ? oc_setreg_32be : oc_setreg_32;
			i2c->getreg = be ? oc_getreg_32be : oc_getreg_32;
			break;

		default:
			dev_err_ratelimited(&pdev->dev, "Unsupported I/O width (%d)\n",
				i2c->reg_io_width);
			ret = -EINVAL;
			goto err_clk;
		}
	}

	init_waitqueue_head(&i2c->wait);

    if (ocores_xfer_mode) {
	    irq = platform_get_irq(pdev, 0);
    } else {
        irq = NONE_IRQ;
    }
	if (irq < 0) {
		ocores_algorithm.master_xfer = ocores_xfer_polling;
	}

	if (ocores_algorithm.master_xfer != ocores_xfer_polling) {
		ret = devm_request_any_context_irq(&pdev->dev, irq,
						   ocores_isr, 0,
						   pdev->name, i2c);
		if (ret) {
			dev_err_ratelimited(&pdev->dev, "Cannot claim IRQ\n");
			goto err_clk;
		}
	}

	ret = ocores_init(&pdev->dev, i2c);
	if (ret)
		goto err_clk;

	/* hook up driver to tree */
	platform_set_drvdata(pdev, i2c);

	ocores_adapter.owner = THIS_MODULE;
	ocores_adapter.class = I2C_CLASS_DEPRECATED;
	ocores_adapter.algo = &ocores_algorithm;
	ocores_adapter.nr = pdev->id + i2c_bus_base;
	ocores_adapter.retries = i2c_retry_times;
    ocores_adapter.timeout = i2c_retry_delay * HZ;
    ocores_adapter.bus_recovery_info = &i2c_ocores_bus_recovery_info;

	snprintf(ocores_adapter.name, sizeof(ocores_adapter.name),
            "fpga-i2c-ocores-%d", pdev->id + i2c_bus_base);

	i2c->adap = ocores_adapter;
	
	i2c_set_adapdata(&i2c->adap, i2c);
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.dev.of_node = pdev->dev.of_node;

	/* add i2c adapter to i2c tree */
	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret)
		goto err_clk;

	ret = sysfs_create_group(&pdev->dev.kobj, &i2c_ocores_attr_grp);
    if (unlikely(ret < 0)) {
        dev_err_ratelimited(&pdev->dev, "I2C ocores adapter %d create sysfs fail!\n", pdev->id);
		i2c_del_adapter(&i2c->adap);
        goto err_clk;
    }

	return 0;

err_clk:
	clk_disable_unprepare(i2c->clk);
	return ret;
}

static int ocores_i2c_remove(struct platform_device *pdev)
{
	struct ocores_i2c *i2c = platform_get_drvdata(pdev);
	u8 ctrl = oc_getreg(i2c, OCI2C_CONTROL);

	/* disable i2c logic */
	ctrl &= ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN);
	oc_setreg(i2c, OCI2C_CONTROL, ctrl);

	/* remove adapter & data */
	i2c_del_adapter(&i2c->adap);

	if (!IS_ERR(i2c->clk))
		clk_disable_unprepare(i2c->clk);

	sysfs_remove_group(&pdev->dev.kobj, &i2c_ocores_attr_grp);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ocores_i2c_suspend(struct device *dev)
{
	struct ocores_i2c *i2c = dev_get_drvdata(dev);
	u8 ctrl = oc_getreg(i2c, OCI2C_CONTROL);

	/* make sure the device is disabled */
	ctrl &= ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN);
	oc_setreg(i2c, OCI2C_CONTROL, ctrl);

	if (!IS_ERR(i2c->clk))
		clk_disable_unprepare(i2c->clk);
	return 0;
}

static int ocores_i2c_resume(struct device *dev)
{
	struct ocores_i2c *i2c = dev_get_drvdata(dev);

	if (!IS_ERR(i2c->clk)) {
		unsigned long rate;
		int ret = clk_prepare_enable(i2c->clk);

		if (ret) {
			dev_err_ratelimited(dev,
				"clk_prepare_enable failed: %d\n", ret);
			return ret;
		}
		rate = clk_get_rate(i2c->clk) / 1000;
		if (rate)
			i2c->ip_clock_khz = rate;
	}
	return ocores_init(dev, i2c);
}

static SIMPLE_DEV_PM_OPS(ocores_i2c_pm, ocores_i2c_suspend, ocores_i2c_resume);
#define OCORES_I2C_PM	(&ocores_i2c_pm)
#else
#define OCORES_I2C_PM	NULL
#endif

static const struct platform_device_id fpga_ocores_devid[] ={
    {.name = "fpga_ocores_dev"}, 
    {},
};

static struct platform_driver ocores_i2c_driver = {
	.probe   = ocores_i2c_probe,
	.remove  = ocores_i2c_remove,
	.driver  = {
		.name = "ocores-i2c",
		.pm = OCORES_I2C_PM,
	},
    .id_table	= fpga_ocores_devid,
};

module_platform_driver(ocores_i2c_driver);

MODULE_AUTHOR("baotuspringswitch baotuspringswitch@gmail.com");
MODULE_DESCRIPTION("Daminglake fpga ocores i2c adapter driver");
MODULE_LICENSE("GPL");
