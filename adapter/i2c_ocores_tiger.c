/*
 * i2c_opencore_driver.c - driver for PORT_CPLD i2c adapter
 * and the module used to register the i2c-opencore-x's device driver, this
 * driver module matches multiple devices.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <asm/ptrace.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/dmi.h>
#include <linux/hwmon-sysfs.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/iopoll.h>

#include "i2c_ocores.h"

struct tasklet_struct port1_i2c_opencores_tasklet;
struct tasklet_struct port2_i2c_opencores_tasklet;

/* PREDEFINED I2C SWITCH DEVICE TOPOLOGY */
static struct i2c_bus i2c_opencore_list[] = {
    {I2C_OPENCORE_CH_1, OCORES_DEVICE1_NAME},
    {I2C_OPENCORE_CH_2, OCORES_DEVICE2_NAME},
    {I2C_OPENCORE_CH_3, OCORES_DEVICE3_NAME},
    {I2C_OPENCORE_CH_4, OCORES_DEVICE4_NAME},
    {I2C_OPENCORE_CH_11, OCORES_DEVICE5_NAME},
    {I2C_OPENCORE_CH_12, OCORES_DEVICE6_NAME},
    {I2C_OPENCORE_CH_13, OCORES_DEVICE7_NAME},
    {I2C_OPENCORE_CH_14, OCORES_DEVICE8_NAME}
};

#define PARAM_HELP \
"enable select driver features:\n"\
"    \t\tnothing parameter used is the ERROR that it do nothing.\n"\
"    \t\tenable_features=0x01 use polling mode.\n"\
"    \t\tenable_features=0x02 use interrupts mode.\n"

static unsigned int enable_features;
module_param(enable_features, uint, 0644);
MODULE_PARM_DESC(enable_features, PARAM_HELP);

static u8 loglevel = 0x00;
static struct mutex i2c_master_locks[I2C_OPENCORE_CH_TOTAL];
static struct i2c_dev_data i2c_dev_data[I2C_OPENCORE_CH_TOTAL];


static int i2c_wait_ack(uint16_t base_addr, unsigned long timeout, int writing){
    int statue;
    int error = 0;
    int delay_us = 1;
    unsigned long timeout_us = timeout*1000;

    error = readx_poll_timeout_atomic(inb, I2C_MASTER_SR_REG(base_addr), statue,
                !(statue & OPENCORES_I2C_SR_BIT_TIP), delay_us, timeout_us);
    statue = IORD_OPENCORES_I2C_SR(base_addr);

    if(error < 0) {
        pr_debug("statue %2.2X\n", statue);
        return error;
    }

    if(statue & OPENCORES_I2C_SR_BIT_TIP) {
        pr_debug("Error Unfinish\n");
        return -EIO;
    }

    if(statue & OPENCORES_I2C_SR_BIT_AL) {
        pr_debug("Error MAL\n");
        return -EAGAIN;
    }

    if(statue & OPENCORES_I2C_SR_BIT_RACK) {
        pr_debug("SL No Acknowlege\n");
        if(writing) {
            pr_debug("Error No Acknowlege\n");
            return -ENXIO;
        }
    } else {
        pr_debug("SL Acknowlege\n");
    }

    return 0;
}

static int lpc_to_i2c_poll_process(struct i2c_dev_data *dev_data)
{
    int error = 0;
    int cnt = 0;
    int bid = 0;
    int i = 0;
    struct i2c_msg *msgs = dev_data->msg;
    uint16_t base_addr = dev_data->start_addr;

    if(dev_data->i2c_adapter.master_bus < I2C_OPENCORE_CH_1 ||
            dev_data->i2c_adapter.master_bus > I2C_OPENCORE_CH_TOTAL){
        error = -ENXIO;
        goto Done;
    }

    for (i = 0; i < dev_data->nmsgs; i++) {
        /* [S][ADDR/R] */
        if(msgs->flags & I2C_M_RD){
            IOWR_OPENCORES_I2C_TXR(msgs->addr << 1 | 0x01, base_addr);
            IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_STA | OPENCORES_I2C_CR_BIT_WR | OPENCORES_I2C_CR_BIT_IACK, base_addr);
        } else {
            IOWR_OPENCORES_I2C_TXR(msgs->addr << 1 | 0x00, base_addr);
            IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_STA | OPENCORES_I2C_CR_BIT_WR | OPENCORES_I2C_CR_BIT_IACK, base_addr);
        }
        pr_debug("I2C Start");

        /* Wait {A} */
        error = i2c_wait_ack(base_addr, 12, 1);
        if(error < 0) {
            pr_debug( "get error %d\n",error);
            goto Done;
        }

        cnt = msgs->len;
        if(msgs->flags & I2C_M_RD){
            /* recieve[DATA] */
            pr_debug( "I2C READ\n");
            for (bid = 0; bid < cnt; bid++) {
                if (bid == cnt-1) {
                    pr_debug( "SET NAK\n");
                    IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_RD | OPENCORES_I2C_CR_BIT_ACK | OPENCORES_I2C_CR_BIT_IACK, base_addr);
                } else {
                    pr_debug( "Reapated read\n");
                    IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_RD | OPENCORES_I2C_CR_BIT_IACK, base_addr);
                }

                /* Wait for byte transfer */
                error = i2c_wait_ack(base_addr, 12, 0);
                if (error < 0) {
                    goto Done;
                }

                msgs->buf[bid] = IORD_OPENCORES_I2C_RXR(base_addr);
                pr_debug( "DATA IN [%d] %2.2X\n",bid,msgs->buf[bid]);
            }
        } else {
            /* write[DATA] */
            pr_debug("I2C WRITE\n");
            for (bid = 0; bid < cnt; bid++) {
                IOWR_OPENCORES_I2C_TXR(msgs->buf[bid], base_addr);
                IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_WR | OPENCORES_I2C_CR_BIT_IACK, base_addr);
                pr_debug("Data > %2.2X\n", msgs->buf[bid]);

                /* Wait {A} */
                error = i2c_wait_ack(base_addr, 12, 1);
                if (error < 0) {
                    goto Done;
                }
            }
        }
        ++msgs;
    }

Done:
    IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_STO | OPENCORES_I2C_CR_BIT_IACK, base_addr);
    /* Add delay to avoid error between two i2c access */
    usleep_range(200, 300);
    pr_debug("MS STOP, Error code %d\n",error);

    return error;
}

/* I2C algorithm */
static int i2c_access(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num)
{
    int ret;
    uint16_t base_addr;
    struct i2c_dev_data *dev_data = i2c_get_adapdata(adapter);
    base_addr = dev_data->start_addr;
    if (dev_data->i2c_adapter.master_bus < I2C_OPENCORE_CH_1 ||
            dev_data->i2c_adapter.master_bus > I2C_OPENCORE_CH_TOTAL) {
        dev_err(&adapter->dev, "i2c_adapter.master_bus=%d.\n", dev_data->i2c_adapter.master_bus);
        ret = -ENXIO;
        return ret;
    }
    dev_data->msg = msgs;
    dev_data->pos = 0;
    dev_data->nmsgs = num;
    dev_data->state = STATE_START;
#if DEBUG_IRQ_STATUS
    dev_data->irq_num = 0;
    dev_data->call_irq_num = 1;
#endif

    // dev_warn(&adapter->dev, "msgs->len=%d\n", msgs->len);
    if (dev_data->i2c_xfer_type == XFER_INTERRUPT) {
        ret = -ETIMEDOUT;
        /* I2C_M_RD: read data, from slave to master */
        if (msgs->flags & I2C_M_RD) {
            IOWR_OPENCORES_I2C_TXR(msgs->addr << 1 | 0x01, base_addr);
            IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_STA | OPENCORES_I2C_CR_BIT_WR | OPENCORES_I2C_CR_BIT_IACK, base_addr);
        } else {
            /* write value(0xa0) into reg(0x0703) */
            IOWR_OPENCORES_I2C_TXR(msgs->addr << 1 | 0x00, base_addr);
            /* write value(0x91) into reg(0x0704) */
            IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_STA | OPENCORES_I2C_CR_BIT_WR | OPENCORES_I2C_CR_BIT_IACK, base_addr);
        }
        if (wait_event_timeout(dev_data->wait, (dev_data->state == STATE_ERROR) || (dev_data->state == STATE_DONE), HZ)) {
            if (likely(dev_data->state == STATE_DONE)) {
                ret = num;
               /* dev_info(&adapter->dev, "dev_data[0x%04x]->state done, call_access=0x%04x, irq_num=0x%04x\n",
                    base_addr,dev_data->call_irq_num, dev_data->irq_num); */
            } else {
                ret = -EIO;
                if (unlikely(loglevel & DEBUG_LEVEL))
                    dev_dbg(&adapter->dev, "dev_data[0x%04x]->state [%d], call_access=0x%04x, irq_num=0x%04x\n",
                        base_addr, dev_data->state, dev_data->call_irq_num, dev_data->irq_num);
            }
        } else {
            /* if timesout: stop ocores i2c devices instance */
            spin_lock_irqsave(&dev_data->spinlock, dev_data->flags);
            dev_data->state = STATE_ERROR;
            IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_IACK | OPENCORES_I2C_CR_BIT_STO, base_addr);
            spin_unlock_irqrestore(&dev_data->spinlock, dev_data->flags);
            if(printk_ratelimit()){
                dev_err(&adapter->dev, "wait_event_timeout invoke timesout. err-code=%d, "
                    "gpe0_ctl_reg[0x%02x], gpe0_sta_reg[0x%02x]\n",
                    ret, inb(GPE0_STS_CTRL_REG), inb(GPE0_STS_STA_REG));
            }
        }
    } else {
        mutex_lock(&i2c_master_locks[dev_data->i2c_adapter.master_bus - 1]);
        ret = lpc_to_i2c_poll_process(dev_data);
        mutex_unlock(&i2c_master_locks[dev_data->i2c_adapter.master_bus - 1]);
        if (ret >= 0) {
            ret = num;
        }
    }

    return ret;
}

/* the macro read status-register PRINTK_REG_VAL_TIMES times */
#define DEBUG_STATUS_REG_VALUE  1

#if DEBUG_STATUS_REG_VALUE
#define PRINTK_REG_VAL_TIMES    1
/* outb(0x02, 0x07fe);  \ */
#define print_reg_value(base_addr, times)   \
do {    \
    if (unlikely(loglevel & DEBUG_LEVEL))   \
        pr_debug("adapterX[0x%04x], SR-val[0x%02x]\n", base_addr, IORD_OPENCORES_I2C_SR(base_addr));  \
} while (--times)
#else
#define PRINTK_REG_VAL_TIMES    1
#define print_reg_value(base_addr, times)
#endif

static void lpc_to_i2c_int_process(struct i2c_dev_data *dev_data)
{
    u8 status;
    u8 print_reg_loop;
    uint16_t base_addr;
    struct i2c_msg *msg;
    msg = dev_data->msg;
    base_addr = dev_data->start_addr;
    print_reg_loop = PRINTK_REG_VAL_TIMES;
#if DEBUG_IRQ_STATUS
    dev_data->irq_num += 1;
#endif

    spin_lock_irqsave(&dev_data->spinlock, dev_data->flags);

    if (unlikely(dev_data->state == STATE_DONE)) {
        IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_IACK | OPENCORES_I2C_CR_BIT_STO, base_addr);
        dev_data->state = STATE_STOP;
        if (unlikely(loglevel & DEBUG_LEVEL))
            pr_debug("lpc-i2c rec-interrupt, dev-state:0x%02x, addr(0x%04x), "
                "gpe0_ctl_reg[0x%02x], gpe0_sta_reg[0x%02x]\n",
                dev_data->state, base_addr, inb(GPE0_STS_CTRL_REG), inb(GPE0_STS_STA_REG));
        goto ERR_OUT;
    }

    status = IORD_OPENCORES_I2C_SR(base_addr);
    if (unlikely((dev_data->state == STATE_STOP) || (dev_data->state == STATE_ERROR))) {
        IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_IACK, base_addr);
        if (dev_data->state == STATE_STOP) {
            dev_data->state = STATE_DONE;
        }
        wake_up(&dev_data->wait);
        goto ERR_OUT;
    }

    if (unlikely(status & OPENCORES_I2C_SR_BIT_AL)) {
        if (unlikely(loglevel & DEBUG_LEVEL))
            pr_debug("Error MAL\n");
        print_reg_value(base_addr, print_reg_loop);
        dev_data->state = STATE_ERROR;
        IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_IACK | OPENCORES_I2C_CR_BIT_STO, base_addr);
        goto ERR_OUT;
    }

    if ((dev_data->state == STATE_START) || (dev_data->state == STATE_WRITE)) {
        if (msg->flags & I2C_M_RD) {
            dev_data->state = STATE_READ;
        } else {
            dev_data->state = STATE_WRITE;
        }
        if (unlikely(status & OPENCORES_I2C_SR_BIT_RACK)) {
            if (unlikely(loglevel & DEBUG_LEVEL))
                pr_debug("Error RxACK\n");
            print_reg_value(base_addr, print_reg_loop);
            dev_data->state = STATE_ERROR;
            IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_IACK | OPENCORES_I2C_CR_BIT_STO, base_addr);
            goto ERR_OUT;
        }
    } else {
        msg->buf[dev_data->pos++] = IORD_OPENCORES_I2C_RXR(base_addr);
    }

    /* end of msg? */
    if (dev_data->pos == msg->len) {
        dev_data->nmsgs--;
        dev_data->msg++;
        dev_data->pos = 0;
        msg = dev_data->msg;

        if (dev_data->nmsgs) { /* end? */
            /* send start? */
            if (!(msg->flags & I2C_M_NOSTART)) {
                u8 addr = (msg->addr << 1);
                if (msg->flags & I2C_M_RD)
                    addr |= 1;
                dev_data->state = STATE_START;
                IOWR_OPENCORES_I2C_TXR(addr, base_addr);
                IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_STA | OPENCORES_I2C_CR_BIT_WR | OPENCORES_I2C_CR_BIT_IACK, base_addr);
                goto ERR_OUT;
            } else {
                if (msg->flags & I2C_M_RD) {
                    dev_data->state = STATE_READ;
                } else {
                    dev_data->state = STATE_WRITE;
                }
            }
        } else {
            dev_data->state = STATE_STOP;
            IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_IACK | OPENCORES_I2C_CR_BIT_STO, base_addr);
            goto ERR_OUT;
        }
    }

    if (dev_data->state == STATE_READ) {
        if (dev_data->pos == (msg->len - 1)) {
            IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_NACK | OPENCORES_I2C_CR_BIT_IACK | OPENCORES_I2C_CR_BIT_RD, base_addr);
        } else {
            IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_ACK & (OPENCORES_I2C_CR_BIT_IACK | OPENCORES_I2C_CR_BIT_RD), base_addr);
        }
    } else {
        IOWR_OPENCORES_I2C_TXR(msg->buf[dev_data->pos++], base_addr);
        IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_WR | OPENCORES_I2C_CR_BIT_IACK, base_addr);
    }

ERR_OUT:
    spin_unlock_irqrestore(&dev_data->spinlock, dev_data->flags);
}

#if 0
void port1_i2c_opencore_work(unsigned long dev_data)
{
    u8 adapter_idx;
    u8 interrupter_flag;
    u8 adapter_interrupt_flag;

    struct i2c_dev_data *opencore_data = (struct i2c_dev_data*)dev_data;
    /* query whether all IPC controllers in the CPLD' i2c-master-adapterX have
     * interrupt requests, noe by one and process the requests */
    interrupter_flag = inb(CPLD1_OPENCORES_I2C_INT_FLAG);
    for(adapter_idx=0; adapter_idx < I2C_OPENCORE_CH_4; ++adapter_idx) {
        adapter_interrupt_flag = interrupter_flag & (1 << adapter_idx);
        if (adapter_interrupt_flag) {
            /* handl2 i2c nuclear interrupt request */
            lpc_to_i2c_int_process(&opencore_data[adapter_idx]);
        }
    }

    outb(GPE0_STS_CTRL_REG_OPEN, GPE0_STS_CTRL_REG);
    outb(OPENCORES_I2C_SW_FINISH_STA, CPLD1_OPENCORES_I2C_INT_REG);
    return ;
}
DECLARE_TASKLET(port1_i2c_opencores_tasklet, port1_i2c_opencore_work, (unsigned long)i2c_dev_data);
#endif

static irqreturn_t handle_i2c_opencore1_int(int irqno, void *i2c_dev)
{
    u8 adapter_idx;
    u8 interrupter_flag;
    u8 adapter_interrupt_flag;
    /* read GPE0_STS(0x422) bit18 lookup the interrupt source */
    interrupter_flag = inb(GPE0_STS_STA_REG);
    interrupter_flag = interrupter_flag & (1<<3);
    if(!interrupter_flag) {
        return IRQ_NONE;
    }

    outb(inb(GPE0_STS_CTRL_REG) & ~(1<<3), GPE0_STS_CTRL_REG);
    outb(OPENCORES_I2C_SW_START_STA, CPLD1_OPENCORES_I2C_INT_REG);
    outb(GPE0_STS_STA_REG_PORT1_CLEAR, GPE0_STS_STA_REG); /* clear register state */

    /* query whether all IPC controllers in the CPLD' i2c-master-adapterX have
     * interrupt requests, noe by one and process the requests */
    interrupter_flag = inb(CPLD1_OPENCORES_I2C_INT_FLAG);
    for(adapter_idx=0; adapter_idx < I2C_OPENCORE_CH_4; ++adapter_idx) {
        adapter_interrupt_flag = interrupter_flag & (1 << adapter_idx);
        if (adapter_interrupt_flag) {
            /* handle1 i2c nuclear interrupt request */
            lpc_to_i2c_int_process(&i2c_dev_data[adapter_idx]);
        }
    }

    outb(inb(GPE0_STS_CTRL_REG) | (1<<3), GPE0_STS_CTRL_REG);
    outb(OPENCORES_I2C_SW_FINISH_STA, CPLD1_OPENCORES_I2C_INT_REG);

    return IRQ_HANDLED;
}

#if 0
void port2_i2c_opencore_work(unsigned long dev_data)
{
    u8 adapter_idx;
    u8 interrupter_flag;
    u8 adapter_interrupt_flag;
    struct i2c_dev_data *opencore_data = (struct i2c_dev_data*)dev_data;

    /* query whether all IPC controllers in the CPLD' i2c-master-adapterX have
     * interrupt requests, one by one and process the requests */
    interrupter_flag = inb(CPLD2_OPENCORES_I2C_INT_FLAG);
    for(adapter_idx=I2C_OPENCORE_CH_11-1; adapter_idx < I2C_OPENCORE_CH_14; ++adapter_idx) {
        adapter_interrupt_flag = interrupter_flag & (1 << adapter_idx);
        if (adapter_interrupt_flag) {
            /* handl2 i2c nuclear interrupt request */
            lpc_to_i2c_int_process(&opencore_data[adapter_idx]);
        }
    }

    outb(GPE0_STS_CTRL_REG_OPEN, GPE0_STS_CTRL_REG);
    outb(OPENCORES_I2C_SW_FINISH_STA, CPLD1_OPENCORES_I2C_INT_REG);
    return ;
}
DECLARE_TASKLET(port2_i2c_opencores_tasklet, port2_i2c_opencore_work, (unsigned long)i2c_dev_data);
#endif

static irqreturn_t handle_i2c_opencore2_int(int irqno, void *i2c_dev)
{
    u8 adapter_idx;
    u8 interrupter_flag;
    u8 adapter_interrupt_flag;
    u8 interrupt_index;
    /* read GPE0_STS(0x422) bit19 lookup the interrupt source */
    interrupter_flag = inb(GPE0_STS_STA_REG);
    interrupter_flag = interrupter_flag & (1<<4);
    if(!interrupter_flag) {
        return IRQ_NONE;
    }

    outb(inb(GPE0_STS_CTRL_REG) & ~(1<<4), GPE0_STS_CTRL_REG);
    outb(OPENCORES_I2C_SW_START_STA, CPLD2_OPENCORES_I2C_INT_REG);
    outb(GPE0_STS_STA_REG_PORT2_CLEAR, GPE0_STS_STA_REG); /* clear register state */

    /* query whether all IPC controllers in the CPLD' i2c-master-adapterX have
     * interrupt requests, one by one and process the requests */
    interrupter_flag = inb(CPLD2_OPENCORES_I2C_INT_FLAG);
    interrupt_index = 0;
    for(adapter_idx=I2C_OPENCORE_CH_11-1; adapter_idx < I2C_OPENCORE_CH_14; ++adapter_idx) {
        adapter_interrupt_flag = interrupter_flag & (1 << interrupt_index);
        ++interrupt_index;
        if (adapter_interrupt_flag) {
            /* handle2 i2c nuclear interrupt request */
            lpc_to_i2c_int_process(&i2c_dev_data[adapter_idx]);
        }
    }

    outb(inb(GPE0_STS_CTRL_REG) | (1<<4), GPE0_STS_CTRL_REG);
    outb(OPENCORES_I2C_SW_FINISH_STA, CPLD2_OPENCORES_I2C_INT_REG);

    // tasklet_schedule(&port2_i2c_opencores_tasklet);
    return IRQ_HANDLED;
}


#if 0
//static irqreturn_t handle_i2c_opencore1_int(int irqno, void *dev_id)
static u32 acpi_handle_i2c_int(acpi_handle gpe_device, u32 gpe_number, void *context)
{
    struct i2c_dev_data *dev_data = context;
    //outb(OPENCORES_I2C_SW_START_STA, CPLD1_OPENCORES_I2C_INT_REG);
    //udelay(10);
    //outb(OPENCORES_I2C_SW_FINISH_STA, CPLD1_OPENCORES_I2C_INT_REG);
    if (dev_data) {
        printk(KERN_CRIT "dev_data is not null");
        return ACPI_INTERRUPT_NOT_HANDLED;
    } else {
        printk(KERN_CRIT "dev_data is null");
        return ACPI_INTERRUPT_NOT_HANDLED;
    }
    lpc_to_i2c_int_process(dev_data);

    return ACPI_INTERRUPT_HANDLED;
}
#endif

/* A callback function show available smbus functions */
static u32 lpc_i2c_func(struct i2c_adapter *a)
{
    return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm lpc_to_i2c_algorithm = {
    .master_xfer = i2c_access,
    .functionality = lpc_i2c_func,
};


/* initialization i2c master adapter parameter */
static int i2c_adapter_reg_init(struct port_cpld_data *port_device, int adapter_idx)
{
    uint16_t addr = port_device->adapter_start_addr;
    i2c_dev_data[adapter_idx].i2c_master_adapter_idx = adapter_idx;
    i2c_dev_data[adapter_idx].i2c_adapter.master_bus = i2c_opencore_list[adapter_idx].master_bus;
    strcpy(i2c_dev_data[adapter_idx].i2c_adapter.calling_name, i2c_opencore_list[adapter_idx].calling_name);

    i2c_dev_data[adapter_idx].i2c_xfer_type = port_device->i2c_xfer_type;
    /* recover adapter status */
    IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_IACK, addr);
    IOWR_OPENCORES_I2C_CTR(CPLDx_OPENCORES_CONF_RESET, addr);
    /* set register for frequency and interrupt/polling mode */
    IOWR_OPENCORES_I2C_PRERLO(OPENCORES_I2C_ADAPTER_FRERLO, addr);
    IOWR_OPENCORES_I2C_PRERHI(OPENCORES_I2C_ADAPTER_FRERHI, addr);
    if (port_device->i2c_xfer_type == XFER_INTERRUPT) {
        /* enable I2C adapter for interrupt 0xc0 */
        IOWR_OPENCORES_I2C_CTR(OPENCORES_I2C_CTR_BIT_EN | OPENCORES_I2C_CTR_BIT_IEN, addr);
    } else {
        /* enable I2C adapter */
        mutex_init(&i2c_master_locks[adapter_idx]);
        IOWR_OPENCORES_I2C_CTR(OPENCORES_I2C_CTR_BIT_EN, addr);
    }
    init_waitqueue_head(&i2c_dev_data[adapter_idx].wait);
    pr_debug("parameter initialization is complete, adapter_idx=%d, &[adapter_idx].wait=0x%p\n",
         adapter_idx+1, &i2c_dev_data[adapter_idx].wait);
    return 0;
}

/* *
 * Create virtual I2C bus adapter for switch devices
 * @param  pdev             struct platform_device pointer
 * @param  adapter_id       virtual i2c port id for switch device mapping
 * @param  adapter_bus_offset bus offset for virtual i2c adapter in system
 * @return                  i2c adapter pointer.
 *
 * When adapter_bus_offset is -1, created adapter with dynamic bus number.
 * Otherwise create adapter at i2c bus = adapter_bus_offset + adapter_id.
 */
static struct i2c_adapter *lpc_to_i2c_adapter_init(struct platform_device *pdev, int adapter_id,
        int adapter_bus_offset)
{
    int error;
    struct i2c_adapter *new_adapter;
    struct port_cpld_data *cpld_data = platform_get_drvdata(pdev);

    memset(&i2c_dev_data[adapter_id], 0, sizeof(i2c_dev_data[adapter_id]));
    dev_info(&pdev->dev, "begin create virtual I2C bus adapter%d\n", adapter_id+1);
    new_adapter = kzalloc(sizeof(*new_adapter), GFP_KERNEL);
    if (!new_adapter) {
        dev_err(&pdev->dev, "Cannot alloc i2c adapter for %s", i2c_opencore_list[adapter_id].calling_name);
        return NULL;
    }
    cpld_data->i2c_adapter = new_adapter;

    i2c_adapter_reg_init(cpld_data, adapter_id);
    new_adapter->owner = THIS_MODULE;
    new_adapter->class = I2C_CLASS_DEPRECATED;
    new_adapter->algo = &lpc_to_i2c_algorithm;
    new_adapter->retries = 3;

    /* If the bus offset is -1, use dynamic bus number */
    if (adapter_bus_offset == -1) {
        new_adapter->nr = -1;
    } else {
        new_adapter->nr = adapter_bus_offset + adapter_id;
    }
    new_adapter->algo_data = &i2c_dev_data[adapter_id];
    i2c_dev_data[adapter_id].start_addr = cpld_data->adapter_start_addr;
    i2c_dev_data[adapter_id].call_irq_num = 0;
    i2c_dev_data[adapter_id].irq_num = 0;
    i2c_dev_data[adapter_id].flags = 0;
    spin_lock_init(&(i2c_dev_data[adapter_id].spinlock));
    snprintf(new_adapter->name, sizeof(new_adapter->name),
             "i2c_ocore_tiger:%s", i2c_dev_data[adapter_id].i2c_adapter.calling_name);
    i2c_set_adapdata(new_adapter, &i2c_dev_data[adapter_id]);

    /* PORT_CPLD1: in interrupt mode, only I2C_OPENCORE_CH_1 register request-irq,
     * and others i2c master adapter not register request-irq */
    if (( (cpld_data->i2c_adapter_idx == I2C_OPENCORE_CH_1) && cpld_data->i2c_xfer_type == XFER_INTERRUPT) ) {
        dev_info(&pdev->dev, "i2c master adapter%d will register interrupt, i2c_dev_data[adapter_id]=0x%p\n",
            cpld_data->i2c_adapter_idx, &i2c_dev_data[adapter_id]);
        error = devm_request_irq(&pdev->dev, cpld_data->irq, handle_i2c_opencore1_int, IRQF_SHARED, new_adapter->name,
                    &i2c_dev_data[adapter_id]);
#if 0
        status = acpi_install_gpe_handler(NULL, 12, ACPI_GPE_LEVEL_TRIGGERED,
                    handle_i2c_opencore1_int, (void *)(&i2c_dev_data[adapter_id]));
#endif
        if (error) {
            dev_err(&pdev->dev, "request_irq %d irq req fail! error %d", cpld_data->irq, error);
            return (struct i2c_adapter *)NULL;
        }
    }

    /* PORT_CPLD2: in interrupt mode, only I2C_OPENCORE_CH_11 register request-irq,
     * and others i2c master adapter not register request-irq */
    if (( (cpld_data->i2c_adapter_idx == I2C_OPENCORE_CH_11) && cpld_data->i2c_xfer_type == XFER_INTERRUPT) ) {
        dev_info(&pdev->dev, "i2c master adapter%d will register interrupt, i2c_dev_data[adapter_id]=0x%p\n",
            cpld_data->i2c_adapter_idx, &i2c_dev_data[adapter_id]);
        error = devm_request_irq(&pdev->dev, cpld_data->irq, handle_i2c_opencore2_int, IRQF_SHARED, new_adapter->name,
                    &i2c_dev_data[adapter_id]);
        if (error) {
            dev_err(&pdev->dev, "request_irq %d irq req fail! error %d", cpld_data->irq, error);
            return (struct i2c_adapter *)NULL;
        }
    }

    error = i2c_add_numbered_adapter(new_adapter);
    if (error < 0) {
        dev_err(&pdev->dev, "Cannot add i2c adapter %s", i2c_dev_data[adapter_id].i2c_adapter.calling_name);
        kvfree(new_adapter);
        if (cpld_data->i2c_xfer_type == XFER_INTERRUPT) {
            free_irq(cpld_data->irq, &i2c_dev_data[adapter_id]);
        }
        return (struct i2c_adapter *)NULL;
    }

    dev_info(&pdev->dev, "create virtual I2C bus adapter%d successful.", adapter_id+1);
    return new_adapter;
};

/* get the bus number from i2c_opencore_list->calling_name */
static inline void get_ocore_adapter_id(const char *name, int *adapter_id)
{
    int ocore_idx;
    for(ocore_idx=0; ocore_idx < I2C_OPENCORE_CH_TOTAL; ++ocore_idx) {
        if (!strcmp(i2c_opencore_list[ocore_idx].calling_name, name)) {
            *adapter_id =  i2c_opencore_list[ocore_idx].master_bus;
            return;
        }
    }
    *adapter_id = -1;
    return;
}

#if SUPPORT_HITLESS_UPDATE_STATUS

static u8 hitless_update_status = 0;

static int port1_hitless_base_reg[] = {
    0x0700, 0x0708, 0x0710, 0x0718
};

static int port2_hitless_base_reg[] = {
    0x0900, 0x0908, 0x0910, 0x0918
};

static ssize_t hitless_update_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    u8 value;
    u8 opencore_index;
    struct port_cpld_data *cpld_data;
    if (kstrtou8(buf, 0, &value) < 0) {
        dev_err(dev, "failed to convert characters to int number\n");
        return -EINVAL;
    }
    hitless_update_status = value;
    cpld_data = dev_get_drvdata(dev);

    switch (value) {
        case PORT_CPLD1_OPENCORE_DISABLE:
            dev_warn(dev, "the Hitless operation will be performed, "
                "which disable the port_cpld1 OpenCore function, cmd-value(0x%02x).\n", value);
            outb(CPLDx_OPENCORES_CONF_RESET, CPLD1_OPENCORES_ADAPTER_CLEAN);
            outb(OPENCORES_I2C_SW_START_STA, CPLD1_OPENCORES_I2C_INT_REG);
            outb(OPENCORES_I2C_SW_FINISH_STA, CPLD1_OPENCORES_I2C_INT_REG);
            outb(CPLDx_OPENCORES_CONF_UNRESET, CPLD1_OPENCORES_ADAPTER_CLEAN);
            break;
        case PORT_CPLD2_OPENCORE_DISABLE:
            dev_warn(dev, "the Hitless operation will be performed, "
                "which disable the port_cpld2 OpenCore function, cmd-value(0x%02x).\n", value);
            outb(CPLDx_OPENCORES_CONF_RESET, CPLD2_OPENCORES_ADAPTER_CLEAN);
            outb(OPENCORES_I2C_SW_START_STA, CPLD2_OPENCORES_I2C_INT_REG);
            outb(OPENCORES_I2C_SW_FINISH_STA, CPLD2_OPENCORES_I2C_INT_REG);
            outb(CPLDx_OPENCORES_CONF_UNRESET, CPLD2_OPENCORES_ADAPTER_CLEAN);
            break;
        case PORT_CPLD1_HITLESS:
            dev_info(dev, "work for port_cpld1 hitless, cmd-value(0x%02x).\n", value);
            for (opencore_index = 0; opencore_index < I2C_OPENCORE_CH_4; ++opencore_index) {
                /* recover adapter status */
                IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_IACK, *(port1_hitless_base_reg + opencore_index));
                IOWR_OPENCORES_I2C_CTR(CPLDx_OPENCORES_CONF_RESET, *(port1_hitless_base_reg + opencore_index));
                /* set register for frequency and interrupt/polling mode */
                IOWR_OPENCORES_I2C_PRERLO(OPENCORES_I2C_ADAPTER_FRERLO, *(port1_hitless_base_reg + opencore_index));
                IOWR_OPENCORES_I2C_PRERHI(OPENCORES_I2C_ADAPTER_FRERHI, *(port1_hitless_base_reg + opencore_index));
                if (cpld_data->i2c_xfer_type == XFER_INTERRUPT) {
                    IOWR_OPENCORES_I2C_CTR(OPENCORES_I2C_CTR_BIT_EN | OPENCORES_I2C_CTR_BIT_IEN, *(port1_hitless_base_reg + opencore_index));
                } else {
                    IOWR_OPENCORES_I2C_CTR(OPENCORES_I2C_CTR_BIT_EN, *(port1_hitless_base_reg + opencore_index));
                }
            }
            break;
        case PORT_CPLD2_HITLESS:
            dev_info(dev, "work for port_cpld2 hitless, cmd-value(0x%02x).\n", value);
            for (opencore_index = 0; opencore_index < I2C_OPENCORE_CH_4; ++opencore_index) {
                /* recover adapter status */
                IOWR_OPENCORES_I2C_CR(OPENCORES_I2C_CR_BIT_IACK, *(port2_hitless_base_reg + opencore_index));
                IOWR_OPENCORES_I2C_CTR(CPLDx_OPENCORES_CONF_RESET, *(port2_hitless_base_reg + opencore_index));
                /* set register for frequency and interrupt/polling mode */
                IOWR_OPENCORES_I2C_PRERLO(OPENCORES_I2C_ADAPTER_FRERLO, *(port2_hitless_base_reg + opencore_index));
                IOWR_OPENCORES_I2C_PRERHI(OPENCORES_I2C_ADAPTER_FRERHI, *(port2_hitless_base_reg + opencore_index));
                if (cpld_data->i2c_xfer_type == XFER_INTERRUPT) {
                    IOWR_OPENCORES_I2C_CTR(OPENCORES_I2C_CTR_BIT_EN | OPENCORES_I2C_CTR_BIT_IEN, *(port2_hitless_base_reg + opencore_index));
                } else {
                    IOWR_OPENCORES_I2C_CTR(OPENCORES_I2C_CTR_BIT_EN, *(port2_hitless_base_reg + opencore_index));
                }
            }
            break;
        default:
            dev_err(dev, "the function can not understand cmd-value(0x%02x)\n", value);
            return -EINVAL;
    }

    return count;
}

static ssize_t hitless_update_read(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "0x%02x\n", hitless_update_status);
}
#endif

static ssize_t hitless_reg_value_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    u8 opencore_index;
    char reg_buf[288];
    char *p = reg_buf;

    p = reg_buf;
    dev_info(dev, "show port_cpld1 hitless reg value.\n");
    memset(p, 0, sizeof(reg_buf));
    for(opencore_index=0; opencore_index < I2C_OPENCORE_CH_4; ++opencore_index) {
        snprintf(p, 32, "adapter(0x%04x):0x%02x 0x%02x 0x%02x\n",
            *(port1_hitless_base_reg + opencore_index),
            IORD_OPENCORES_I2C_PRERLO(*(port1_hitless_base_reg + opencore_index)),
            IORD_OPENCORES_I2C_PRERHI(*(port1_hitless_base_reg + opencore_index)),
            IORD_OPENCORES_I2C_CTR(*(port1_hitless_base_reg + opencore_index)));
        p += 31;
    }
    dev_info(dev, "\nport_cpld1 OpenCore reg buf:%s", reg_buf);

    dev_info(dev, "show port_cpld2 hitless reg value.\n");
    for(opencore_index=0; opencore_index < I2C_OPENCORE_CH_4; ++opencore_index) {
        snprintf(p, 32, "adapter(0x%04x):0x%02x 0x%02x 0x%02x\n",
            *(port2_hitless_base_reg + opencore_index),
            IORD_OPENCORES_I2C_PRERLO(*(port2_hitless_base_reg + opencore_index)),
            IORD_OPENCORES_I2C_PRERHI(*(port2_hitless_base_reg + opencore_index)),
            IORD_OPENCORES_I2C_CTR(*(port2_hitless_base_reg + opencore_index)));
        p += 31;
    }

    *p = '\0';
    dev_info(dev, "\nport_cpld2 OpenCore reg buf:%s", reg_buf);

    return snprintf(buf, PAGE_SIZE, "%s", reg_buf);
}

static ssize_t opencore_irq_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    u8 opencore_index;
    char reg_buf[384];
    static struct i2c_dev_data *p_opencore_idx = NULL;
    char *p = NULL;

    p = reg_buf;
    p_opencore_idx = i2c_dev_data;
    memset(p, 0, sizeof(reg_buf));
    for(opencore_index=0; opencore_index < I2C_OPENCORE_CH_TOTAL; ++opencore_index) {
        snprintf(p, 42, "adapter%d: call_irq(0x%04x) irq(0x%06x)\n",
            opencore_index+1, (p_opencore_idx + opencore_index)->call_irq_num,
            (p_opencore_idx + opencore_index)->irq_num);
        // count zero for next debug
        (p_opencore_idx + opencore_index)->call_irq_num = 0;
        (p_opencore_idx + opencore_index)->irq_num = 0;
        p += 41;
    }

    *p = '\0';
    dev_info(dev, "\nOpenCore irq status:%s", reg_buf);

    return snprintf(buf, PAGE_SIZE, "%s", reg_buf);
}

static ssize_t show_loglevel(struct device *dev, struct device_attribute *attr, char *buf)
{
    return snprintf(buf, PAGE_SIZE, "the log level is: 0x%02x\n", loglevel);
}

static ssize_t store_loglevel(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    if (kstrtou8(buf, 0, &loglevel) < 0) {
        dev_err(dev, "invalid loglevel, please set Dec or Hex(0x..) data.\n");
        return -EINVAL;
    }

    return count;
}


static int i2c_adapter_drv_probe(struct platform_device *pdev)
{
    u8 verify_val;
    int adapter_id;
    struct resource *res;
    struct port_cpld_data *cpld_data;
    //dump_stack();

    cpld_data = devm_kzalloc(&pdev->dev, sizeof(struct port_cpld_data), GFP_KERNEL);
    if (!cpld_data) {
        dev_err(&pdev->dev, "devm_kzalloc get port_cpld_data failed.\n");
        return -ENOMEM;
    }
    platform_set_drvdata(pdev, (void *)cpld_data);

    /* enable features on user request. only i2c-master-adapter1 register requset-irq,
     * other adapterX do not register request-irq */
    if (enable_features == 0x02) {
        dev_info(&pdev->dev, "the lpc_i2c_core.ko use interupter mode for i2c master.\n");
        cpld_data->i2c_xfer_type = XFER_INTERRUPT;  /* use interrupt mode */
        cpld_data->irq = ACPI_GPE_IRQ_NO;           /* initialize the interrupt number */
    } else if (enable_features == 0x01) {
        dev_info(&pdev->dev, "the lpc_i2c_core.ko use polling mode for i2c master.\n");
        cpld_data->i2c_xfer_type = XFER_POLLING;    /* use polling mode */
    } else {
        dev_err(&pdev->dev, PARAM_HELP);
        return -1;
    }

    res = platform_get_resource(pdev, IORESOURCE_IO, 0);
    if (unlikely(!res)) {
        dev_err(&pdev->dev, "Specified Resource Not Available...\n");
        return -ENODEV;
    }

    outb(0xed, res->start);
    verify_val = inb(res->start);
    if(0xed != verify_val) {
        dev_err(&pdev->dev, "the probe can not find the corresponding hardware device(0x%llx) "
            "read value(0x%04x) from reg(0x%04llx)\n", res->start, verify_val, res->start);
        return -1;
    }

    /* clear adapter register allocation parameters */
    if (!strcmp(OCORES_DEVICE1_NAME, res->name)) {
        dev_info(&pdev->dev, "resource id res(0x%p), res->name(%s)\n", res, res->name);
        outb(GPE0_STS_CTRL_REG_OPEN, GPE0_STS_CTRL_REG);
        outb(CPLDx_OPENCORES_CONF_RESET, CPLD1_OPENCORES_ADAPTER_CLEAN);
        outb(OPENCORES_I2C_SW_START_STA, CPLD1_OPENCORES_I2C_INT_REG);
        outb(OPENCORES_I2C_SW_FINISH_STA, CPLD1_OPENCORES_I2C_INT_REG);
        outb(CPLDx_OPENCORES_CONF_UNRESET, CPLD1_OPENCORES_ADAPTER_CLEAN);
    } else if (!strcmp(OCORES_DEVICE5_NAME, res->name)) {
        dev_info(&pdev->dev, "resource id res(0x%p), res->name(%s)\n", res, res->name);
        outb(CPLDx_OPENCORES_CONF_RESET, CPLD2_OPENCORES_ADAPTER_CLEAN);
        outb(OPENCORES_I2C_SW_START_STA, CPLD2_OPENCORES_I2C_INT_REG);
        outb(OPENCORES_I2C_SW_FINISH_STA, CPLD2_OPENCORES_I2C_INT_REG);
        outb(CPLDx_OPENCORES_CONF_UNRESET, CPLD2_OPENCORES_ADAPTER_CLEAN);
    }

    dev_info(&pdev->dev, "resource id res(0x%p), res->name(%s)\n", res, res->name);
    get_ocore_adapter_id(res->name, &adapter_id);
    if(-1 == adapter_id) {
        dev_err(&pdev->dev, "get %s id error.\n", res->name);
        return -1;
    }

    cpld_data->adapter_start_addr = res->start;
    cpld_data->i2c_adapter_idx = adapter_id;
    dev_info(&pdev->dev, "port cpld i2c_adapter_idx(%d), start addr(0x%04x)\n",
        cpld_data->i2c_adapter_idx, cpld_data->adapter_start_addr);

    if (!lpc_to_i2c_adapter_init(pdev, adapter_id-1, VIRTUAL_I2C_BUS_OFFSET)) {
        dev_err(&pdev->dev, "create %s failed, start addr 0x%04llx.\n", res->name, res->start);
        return -1;
    } else {
        dev_info(&pdev->dev, "create %s successful, start addr 0x%04llx.\n", res->name, res->start);
    }

    return 0;
}

static int i2c_adapter_drv_remove(struct platform_device *pdev)
{
    struct port_cpld_data *cpld_data;
    /* clear adapter register related allocation parameters */
    outb(CPLDx_OPENCORES_CONF_RESET, CPLD1_OPENCORES_ADAPTER_CLEAN);
    outb(OPENCORES_I2C_SW_START_STA, CPLD1_OPENCORES_I2C_INT_REG);
    outb(OPENCORES_I2C_SW_FINISH_STA, CPLD1_OPENCORES_I2C_INT_REG);
    outb(CPLDx_OPENCORES_CONF_UNRESET, CPLD1_OPENCORES_ADAPTER_CLEAN);

    outb(CPLDx_OPENCORES_CONF_RESET, CPLD2_OPENCORES_ADAPTER_CLEAN);
    outb(OPENCORES_I2C_SW_START_STA, CPLD2_OPENCORES_I2C_INT_REG);
    outb(OPENCORES_I2C_SW_FINISH_STA, CPLD2_OPENCORES_I2C_INT_REG);
    outb(CPLDx_OPENCORES_CONF_UNRESET, CPLD2_OPENCORES_ADAPTER_CLEAN);

    cpld_data = platform_get_drvdata(pdev);
    if (cpld_data->i2c_adapter != NULL) {
        i2c_del_adapter(cpld_data->i2c_adapter);
    }
    dev_info(&pdev->dev, "port-cpld1 adapter1 delete successful.\n");
    return 0;
}

static const struct platform_device_id lpc_i2c_master_adapter_idx[] ={
    {OCORES_DEVICE1_NAME, 0},
    {OCORES_DEVICE2_NAME, 1},
    {OCORES_DEVICE3_NAME, 2},
    {OCORES_DEVICE4_NAME, 3},
    {OCORES_DEVICE5_NAME, 4},
    {OCORES_DEVICE6_NAME, 5},
    {OCORES_DEVICE7_NAME, 6},
    {OCORES_DEVICE8_NAME, 7},
    {},
};
MODULE_DEVICE_TABLE(platform, lpc_i2c_master_adapter_idx);


static struct platform_driver i2c_adapter_driver = {
    .driver = {
        .name = "i2c_OCoreX",
    },
    .probe = i2c_adapter_drv_probe,
    .remove = __exit_p(i2c_adapter_drv_remove),
    .id_table	= lpc_i2c_master_adapter_idx,
};


#if SUPPORT_HITLESS_UPDATE_STATUS
static SENSOR_DEVICE_ATTR(hitless_update_status, 0644, hitless_update_read, hitless_update_write, 0);
#endif
static SENSOR_DEVICE_ATTR(hitless_reg_show, 0444, hitless_reg_value_show, NULL, 0);
static SENSOR_DEVICE_ATTR(opencore_irq_info, 0444, opencore_irq_info_show, NULL, 0);
static SENSOR_DEVICE_ATTR(debug_log, 0664, show_loglevel, store_loglevel, 0);

static struct attribute *port_cpld_attrs[] = {
#if SUPPORT_HITLESS_UPDATE_STATUS
    &sensor_dev_attr_hitless_update_status.dev_attr.attr,
#endif
    &sensor_dev_attr_hitless_reg_show.dev_attr.attr,
    &sensor_dev_attr_opencore_irq_info.dev_attr.attr,
    &sensor_dev_attr_debug_log.dev_attr.attr,

    NULL,
};

static struct attribute_group port_cpld_attrs_grp = {
    .attrs = port_cpld_attrs,
};


static int port_cpld_drv_probe(struct platform_device *pdev)
{
    u8 verify_val;
    struct resource *res;
    struct port_cpld_data *cpld_data;
    int ret =0;
    dev_info(&pdev->dev, "invoke [%s] create sysfs-node for hitless.\n", __func__);

    outb(GPE0_STS_CTRL_REG_OPEN, GPE0_STS_CTRL_REG);
    cpld_data = devm_kzalloc(&pdev->dev, sizeof(struct port_cpld_data), GFP_KERNEL);
    if (!cpld_data)
        return -ENOMEM;
    platform_set_drvdata(pdev, (void *)cpld_data);

    /* enable features on user request. only i2c-master-adapter1 register requset-irq,
     * other adapterX do not register request-irq */
    if (enable_features == 0x02) {
        dev_info(&pdev->dev, "the lpc_i2c_core.ko use interupter mode for i2c master.\n");
        cpld_data->i2c_xfer_type = XFER_INTERRUPT;  /* use interrupt mode */
        cpld_data->irq = ACPI_GPE_IRQ_NO;           /* initialize the interrupt number */
    } else if (enable_features == 0x01) {
        dev_info(&pdev->dev, "the lpc_i2c_core.ko use polling mode for i2c master.\n");
        cpld_data->i2c_xfer_type = XFER_POLLING;    /* use polling mode */
    } else {
        dev_err(&pdev->dev, PARAM_HELP);
        return -1;
    }

    mutex_init(&cpld_data->cpld_lock);
    res = platform_get_resource(pdev, IORESOURCE_IO, 0);
    if (unlikely(!res)) {
        dev_err(&pdev->dev, "Specified Resource Not Available...\n");
        return -1;
    }

    verify_val = inb(res->start);
    if (!verify_val) {
        dev_err(&pdev->dev, "the probe can not find the corresponding hardware device(0x%llx) "
            "read value(%d) from reg(0x%04llx)\n", res->start, verify_val, res->start);
        return -1;
    }

    ret = sysfs_create_group(&pdev->dev.kobj, &port_cpld_attrs_grp);
    if (ret) {
        dev_err(&pdev->dev, "Cannot create sysfs for ctrl cpld\n");
        return -1;
    }
    dev_info(&pdev->dev, "[%s] create sysfs-node successful.\n", __func__);
    return 0;
}

static int port_cpld_drv_remove(struct platform_device *pdev)
{
    sysfs_remove_group(&pdev->dev.kobj, &port_cpld_attrs_grp);
    return 0;
}

static struct platform_driver port_cpld_drv = {
    .probe  = port_cpld_drv_probe,
    .remove = __exit_p(port_cpld_drv_remove),
    .driver = {
        .name   = PORT_CPLD_NAME,
    },
};

int port_cpld_init(void)
{
    /* register i2c master adapterX */
    platform_driver_register(&i2c_adapter_driver);
    /* register the driver for hitless */
    platform_driver_register(&port_cpld_drv);
    return 0;
}

void port_cpld_exit(void)
{
    /* unregister the driver for hitless */
    platform_driver_unregister(&port_cpld_drv);
    /* unregister i2c master adapterX */
    platform_driver_unregister(&i2c_adapter_driver);
}

module_init(port_cpld_init);
module_exit(port_cpld_exit);

MODULE_AUTHOR("baotuspringswitch@gmail.com");
MODULE_DESCRIPTION("Port CPLD driver for baotuspring I2C OpenCore");
MODULE_LICENSE("GPL");
