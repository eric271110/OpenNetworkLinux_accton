// SPDX-License-Identifier: GPL-2.0
/*
 * i2c-ocores.c: I2C bus driver for OpenCores I2C controller
 * (https://opencores.org/project/i2c/overview)
 *
 * Peter Korsgaard <peter@korsgaard.com>
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
#include <linux/atomic.h>

/*
 * 'process_lock' exists because ocores_process() and ocores_process_timeout()
 * can't run in parallel.
 */
struct ocores_i2c {
    void __iomem *base;
    int iobase;
    u32 reg_shift;
    u32 reg_io_width;
    unsigned long flags;
    wait_queue_head_t wait;
    struct i2c_adapter adap;
    struct i2c_msg *msg;
    int pos;
    int nmsgs;
    int state; /* see STATE_ */
    spinlock_t process_lock;
    struct clk *clk;
    int ip_clock_khz;
    int bus_clock_khz;
    void (*setreg)(struct ocores_i2c *i2c, int reg, u8 value);
    u8 (*getreg)(struct ocores_i2c *i2c, int reg);
};

/* registers */
#define OCI2C_PRELOW		0
#define OCI2C_PREHIGH		1
#define OCI2C_CONTROL		2
#define OCI2C_DATA		3
#define OCI2C_CMD		4 /* write only */
#define OCI2C_STATUS		4 /* read only, same address as OCI2C_CMD */

#define OCI2C_CTRL_IEN		0x40
#define OCI2C_CTRL_EN		0x80

#define OCI2C_CMD_START		0x91
#define OCI2C_CMD_STOP		0x41
#define OCI2C_CMD_READ		0x21
#define OCI2C_CMD_WRITE		0x11
#define OCI2C_CMD_READ_ACK	0x21
#define OCI2C_CMD_READ_NACK	0x29
#define OCI2C_CMD_IACK		0x01

#define OCI2C_STAT_IF		0x01
#define OCI2C_STAT_TIP		0x02
#define OCI2C_STAT_ARBLOST	0x20
#define OCI2C_STAT_BUSY		0x40
#define OCI2C_STAT_NACK		0x80

#define STATE_DONE		0
#define STATE_START		1
#define STATE_WRITE		2
#define STATE_READ		3
#define STATE_ERROR		4

#define OCORES_FLAG_BROKEN_IRQ BIT(1)

#define SPI_BUSY_MASK_CPLD             0x01

#define PORT_NUM 130
/*FPGA SPI MUX*/
#define SPI_MUX_MB_CPLD0               0x0
#define SPI_MUX_MB_CPLD1               0x1
#define SPI_MUX_MEZZ_BOT_L             0x2
#define SPI_MUX_MEZZ_BOT_R             0x3
#define SPI_MUX_MEZZ_TOP_L             0x4
#define SPI_MUX_MEZZ_TOP_R             0x5

static unsigned int timeout = 1;
module_param(timeout, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(timeout, "Timeout for ocores_poll_wait, in milliseconds");

static unsigned int debug = 0;
module_param(debug, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug, "Enable or disable debug message. 1 -> enable, 0 -> disable");

spinlock_t cpld_access_lock;
EXPORT_SYMBOL(cpld_access_lock);

#define LOCK(lock)      \
do {                                                \
    spin_lock(lock);                                \
} while (0)

#define UNLOCK(lock)    \
do {                                                \
    spin_unlock(lock);                              \
} while (0)

#define IOREMAP_SIZE                        (0x04)
void __iomem    *spi_busy_reg=NULL;
void __iomem    *spi_mux_reg = NULL;
EXPORT_SYMBOL(spi_busy_reg);
EXPORT_SYMBOL(spi_mux_reg);

static const int port_mux[PORT_NUM]= {
    /* MEZZ_TOP_L */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port1 */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port2 */
    /* MB_CPLD0 */
    SPI_MUX_MB_CPLD0, /* OSFP port3 */
    SPI_MUX_MB_CPLD0, /* OSFP port4 */
    SPI_MUX_MB_CPLD0, /* OSFP port5 */
    SPI_MUX_MB_CPLD0, /* OSFP port6 */
    /* MEZZ_BOT_L */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port7 */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port8 */
    /* MEZZ_TOP_L */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port9 */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port10 */
    /* MB_CPLD0 */
    SPI_MUX_MB_CPLD0, /* OSFP port11 */
    SPI_MUX_MB_CPLD0, /* OSFP port12 */
    SPI_MUX_MB_CPLD0, /* OSFP port13 */
    SPI_MUX_MB_CPLD0, /* OSFP port14 */
    /* MEZZ_BOT_L */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port15 */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port16 */
    /* MEZZ_TOP_L */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port17 */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port18 */
    /* MB_CPLD0 */
    SPI_MUX_MB_CPLD0, /* OSFP port19 */
    SPI_MUX_MB_CPLD0, /* OSFP port20 */
    SPI_MUX_MB_CPLD0, /* OSFP port21 */
    SPI_MUX_MB_CPLD0, /* OSFP port22 */
    /* MEZZ_BOT_L */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port23 */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port24 */
    /* MEZZ_TOP_L */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port25 */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port26 */
    /* MB_CPLD0 */
    SPI_MUX_MB_CPLD0, /* OSFP port27 */
    SPI_MUX_MB_CPLD0, /* OSFP port28 */
    SPI_MUX_MB_CPLD0, /* OSFP port29 */
    SPI_MUX_MB_CPLD0, /* OSFP port30 */
    /* MEZZ_BOT_L */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port31 */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port32 */
    /* MEZZ_TOP_L */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port33 */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port34 */
    /* MB_CPLD0 */
    SPI_MUX_MB_CPLD0, /* OSFP port35 */
    SPI_MUX_MB_CPLD0, /* OSFP port36 */
    SPI_MUX_MB_CPLD0, /* OSFP port37 */
    SPI_MUX_MB_CPLD0, /* OSFP port38 */
    /* MEZZ_BOT_L */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port39 */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port40 */
    /* MEZZ_TOP_L */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port41 */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port42 */
    /* MB_CPLD0 */
    SPI_MUX_MB_CPLD0, /* OSFP port43 */
    SPI_MUX_MB_CPLD0, /* OSFP port44 */
    SPI_MUX_MB_CPLD0, /* OSFP port45 */
    SPI_MUX_MB_CPLD0, /* OSFP port46 */
    /* MEZZ_BOT_L */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port47 */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port48 */
    /* MEZZ_TOP_L */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port49 */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port50 */
    /* MB_CPLD0 */
    SPI_MUX_MB_CPLD0, /* OSFP port51 */
    SPI_MUX_MB_CPLD0, /* OSFP port52 */
    SPI_MUX_MB_CPLD0, /* OSFP port53 */
    SPI_MUX_MB_CPLD0, /* OSFP port54 */
    /* MEZZ_BOT_L */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port55 */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port56 */
    /* MEZZ_TOP_L */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port57 */
    SPI_MUX_MEZZ_TOP_L, /* OSFP port58 */
    /* MB_CPLD0 */
    SPI_MUX_MB_CPLD0, /* OSFP port59 */
    SPI_MUX_MB_CPLD0, /* OSFP port60 */
    SPI_MUX_MB_CPLD0, /* OSFP port61 */
    SPI_MUX_MB_CPLD0, /* OSFP port62 */
    /* MEZZ_BOT_L */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port63 */
    SPI_MUX_MEZZ_BOT_L, /* OSFP port64 */
    /* MEZZ_TOP_R */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port65 */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port66 */
    /* MB_CPLD1 */
    SPI_MUX_MB_CPLD1, /* OSFP port67 */
    SPI_MUX_MB_CPLD1, /* OSFP port68 */
    SPI_MUX_MB_CPLD1, /* OSFP port69 */
    SPI_MUX_MB_CPLD1, /* OSFP port70 */
    /* MEZZ_BOT_R */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port71 */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port72 */
    /* MEZZ_TOP_R */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port73 */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port74 */
    /* MB_CPLD1 */
    SPI_MUX_MB_CPLD1, /* OSFP port75 */
    SPI_MUX_MB_CPLD1, /* OSFP port76 */
    SPI_MUX_MB_CPLD1, /* OSFP port77 */
    SPI_MUX_MB_CPLD1, /* OSFP port78 */
    /* MEZZ_BOT_R */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port79 */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port80 */
    /* MEZZ_TOP_R */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port81 */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port82 */
    /* MB_CPLD1 */
    SPI_MUX_MB_CPLD1, /* OSFP port83 */
    SPI_MUX_MB_CPLD1, /* OSFP port84 */
    SPI_MUX_MB_CPLD1, /* OSFP port85 */
    SPI_MUX_MB_CPLD1, /* OSFP port86 */
    /* MEZZ_BOT_R */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port87 */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port88 */
    /* MEZZ_TOP_R */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port89 */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port90 */
    /* MB_CPLD1 */
    SPI_MUX_MB_CPLD1, /* OSFP port91 */
    SPI_MUX_MB_CPLD1, /* OSFP port92 */
    SPI_MUX_MB_CPLD1, /* OSFP port93 */
    SPI_MUX_MB_CPLD1, /* OSFP port94 */
    /* MEZZ_BOT_R */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port95 */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port96 */
    /* MEZZ_TOP_R */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port97 */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port98 */
    /* MB_CPLD1 */
    SPI_MUX_MB_CPLD1, /* OSFP port99 */
    SPI_MUX_MB_CPLD1, /* OSFP port100 */
    SPI_MUX_MB_CPLD1, /* OSFP port101 */
    SPI_MUX_MB_CPLD1, /* OSFP port102 */
    /* MEZZ_BOT_R */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port103 */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port104 */
    /* MEZZ_TOP_R */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port105 */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port106 */
    /* MB_CPLD1 */
    SPI_MUX_MB_CPLD1, /* OSFP port107 */
    SPI_MUX_MB_CPLD1, /* OSFP port108 */
    SPI_MUX_MB_CPLD1, /* OSFP port109 */
    SPI_MUX_MB_CPLD1, /* OSFP port110 */
    /* MEZZ_BOT_R */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port111 */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port112 */
    /* MEZZ_TOP_R */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port113 */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port114 */
    /* MB_CPLD1 */
    SPI_MUX_MB_CPLD1, /* OSFP port115 */
    SPI_MUX_MB_CPLD1, /* OSFP port116 */
    SPI_MUX_MB_CPLD1, /* OSFP port117 */
    SPI_MUX_MB_CPLD1, /* OSFP port118 */
    /* MEZZ_BOT_R */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port119 */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port120 */
    /* MEZZ_TOP_R */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port121 */
    SPI_MUX_MEZZ_TOP_R, /* OSFP port122 */
    /* MB_CPLD1 */
    SPI_MUX_MB_CPLD1, /* OSFP port123 */
    SPI_MUX_MB_CPLD1, /* OSFP port124 */
    SPI_MUX_MB_CPLD1, /* OSFP port125 */
    SPI_MUX_MB_CPLD1, /* OSFP port126 */
    /* MEZZ_BOT_R */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port127 */
    SPI_MUX_MEZZ_BOT_R, /* OSFP port128 */
    /* MB_CPLD1 */
    SPI_MUX_MB_CPLD1, /* SFP+ port129 */
    SPI_MUX_MB_CPLD1, /* SFP+ port130 */
};

int wait_spi(u32 mask, u8 times) {
	u32 data;
	u32 ri = 0;
	unsigned long j;

	/* pr_info("CPLD %u, Will time-out at jiffie %lu\n", cpld_id,times); */
	if (!spi_busy_reg) {
		return -EFAULT;
	}

	j = jiffies + times;
	while (1) {
		data = ioread8(spi_busy_reg);
		if (!(((data) & 0xFF) & mask)) {
			break;
		}

		if (time_after(jiffies, j)) {
			if (debug) {
				pr_warn("@ %u, wait_spi TIMEOUT \n", ri);
			}
			return -ETIMEDOUT;
		}

		ri++;
	}

	return 0;
}
EXPORT_SYMBOL(wait_spi);

static inline void oc_setreg(struct ocores_i2c *i2c, int reg, u8 value)
{
    wait_spi(SPI_BUSY_MASK_CPLD, 6);
    i2c->setreg(i2c, reg, value);
}

static inline u8 oc_getreg(struct ocores_i2c *i2c, int reg)
{
    wait_spi(SPI_BUSY_MASK_CPLD, 6);
    return i2c->getreg(i2c, reg);
}

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

static void oc_setreg_io_8(struct ocores_i2c *i2c, int reg, u8 value)
{
    outb(value, i2c->iobase + reg);
}

static inline u8 oc_getreg_io_8(struct ocores_i2c *i2c, int reg)
{
    return inb(i2c->iobase + reg);
}

static void ocores_process(struct ocores_i2c *i2c, u8 stat)
{
    struct i2c_msg *msg = i2c->msg;
    unsigned long flags;
    struct device *dev = i2c->adap.dev.parent;

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
        if (debug) {
			dev_warn(dev, "I2C %s arbitration lost", i2c->adap.name);
        }
        oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
        goto out;
    }

    if ((i2c->state == STATE_START) || (i2c->state == STATE_WRITE)) {
        i2c->state =
            (msg->flags & I2C_M_RD) ? STATE_READ : STATE_WRITE;

        if (stat & OCI2C_STAT_NACK) {
            i2c->state = STATE_ERROR;
            if (debug) {
                dev_warn(dev, "I2C %s, no ACK from slave 0x%02x",
					 i2c->adap.name, msg->addr);
            }
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
        oc_setreg(i2c, OCI2C_CMD, i2c->pos == (msg->len - 1) ?
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

    if (i2c->flags & OCORES_FLAG_BROKEN_IRQ) {
        if ((stat & OCI2C_STAT_IF) && !(stat & OCI2C_STAT_BUSY))
            return IRQ_NONE;
    } else if (!(stat & OCI2C_STAT_IF)) {
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
    err = ocores_wait(i2c, OCI2C_STATUS, mask, 0, msecs_to_jiffies(timeout));
    if (err) {
        if (debug) {
			dev_warn(i2c->adap.dev.parent,
				 "%s: STATUS timeout, bit 0x%x did not clear in %ums(msecs_to_jiffies(%u)=%lu)\n",
                     __func__, mask, timeout, timeout, msecs_to_jiffies(timeout));
        }
    }
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
    int err;

    while (1) {
        err = ocores_poll_wait(i2c);
        if (err) {
            break; /* timeout */
        }

        ret = ocores_isr(-1, i2c);
        if (ret == IRQ_NONE)
            break; /* all messages have been transferred */
        else {
            if (i2c->flags & OCORES_FLAG_BROKEN_IRQ)
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
    int fpga_spi_mux_data;

    LOCK(&cpld_access_lock);

    struct platform_device *pdev;
    struct device *dev;
    int port;

    if (!i2c->adap.dev.parent) {
        UNLOCK(&cpld_access_lock);
        return -EFAULT;
    }

    /* Get FPGA spi mux from pdev->id */
    dev = i2c->adap.dev.parent;
    pdev = container_of(dev, struct platform_device, dev);
    port = (pdev->id & 0x00FF);
    fpga_spi_mux_data = port_mux[port];
    iowrite8(fpga_spi_mux_data, spi_mux_reg);

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
        UNLOCK(&cpld_access_lock);
        return ret;
    }

    UNLOCK(&cpld_access_lock);
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
    u8 ctrl;

    LOCK(&cpld_access_lock);
    ctrl = oc_getreg(i2c, OCI2C_CONTROL);

    /* make sure the device is disabled */
    ctrl &= ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN);
    oc_setreg(i2c, OCI2C_CONTROL, ctrl);

    prescale = (i2c->ip_clock_khz / (5 * i2c->bus_clock_khz)) - 1;
    prescale = clamp(prescale, 0, 0xffff);

    diff = i2c->ip_clock_khz / (5 * (prescale + 1)) - i2c->bus_clock_khz;
    if (abs(diff) > i2c->bus_clock_khz / 10) {
        UNLOCK(&cpld_access_lock);
        dev_err(dev,
                "Unsupported clock settings: core: %d KHz, bus: %d KHz\n",
                i2c->ip_clock_khz, i2c->bus_clock_khz);
        return -EINVAL;
    }

    oc_setreg(i2c, OCI2C_PRELOW, prescale & 0xff);
    oc_setreg(i2c, OCI2C_PREHIGH, prescale >> 8);

    /* Init the device */
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);
    oc_setreg(i2c, OCI2C_CONTROL, ctrl | OCI2C_CTRL_EN);
    UNLOCK(&cpld_access_lock);

    dev_info(dev, "OCI2C_PRELOW=0x%02x OCI2C_PREHIGH=0x%02x\n",
                  prescale & 0xff, prescale >> 8);

    return 0;
}


static u32 ocores_func(struct i2c_adapter *adap)
{
    return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm ocores_algorithm = {
    .master_xfer = ocores_xfer,
    .functionality = ocores_func,
};

static const struct i2c_adapter ocores_adapter = {
    .owner = THIS_MODULE,
    .name = "i2c-ocores",
    .class = I2C_CLASS_DEPRECATED,
    .algo = &ocores_algorithm,
};

static int ocores_i2c_probe(struct platform_device *pdev)
{
    struct ocores_i2c *i2c;
    struct ocores_i2c_platform_data *pdata;
    struct resource *res;
    int ret;
    int i;

    i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
    if (!i2c)
        return -ENOMEM;

    spin_lock_init(&i2c->process_lock);

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (res) {
        /*
         * Use devm_ioremap() instead of devm_ioremap_resource() because
         * multiple ocores platform devices share the same physical FPGA
         * I2C master registers (differentiated by SPI mux selection).
         * devm_ioremap_resource() requests exclusive access which fails
         * with -EBUSY when a second device tries to map the same region.
         */
        i2c->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
        dev_info(&pdev->dev, "Resource start:0x%llx, end:0x%llx", res->start, res->end);
        if (!i2c->base)
            return -ENOMEM;
    } else {
        return -EINVAL;
    }

    pdata = dev_get_platdata(&pdev->dev);
    if (!pdata) {
        dev_err(&pdev->dev, "No platform data\n");
        return -EINVAL;
    }

    i2c->reg_shift = pdata->reg_shift;
    i2c->reg_io_width = pdata->reg_io_width;
    i2c->ip_clock_khz = pdata->clock_khz;
    i2c->bus_clock_khz = 100;

    if (i2c->reg_io_width == 0)
        i2c->reg_io_width = 1;

    switch (i2c->reg_io_width) {
    case 1:
        i2c->setreg = oc_setreg_8;
        i2c->getreg = oc_getreg_8;
        break;
    case 2:
        i2c->setreg = oc_setreg_16;
        i2c->getreg = oc_getreg_16;
        break;
    case 4:
        i2c->setreg = oc_setreg_32;
        i2c->getreg = oc_getreg_32;
        break;
    default:
        dev_err(&pdev->dev, "Unsupported I/O width (%d)\n",
                i2c->reg_io_width);
        return -EINVAL;
    }

    init_waitqueue_head(&i2c->wait);

    /* This platform uses polling mode (no IRQ from FPGA) */
    ocores_algorithm.master_xfer = ocores_xfer_polling;

    ret = ocores_init(&pdev->dev, i2c);
    if (ret)
        return ret;

    /* hook up driver to tree */
    platform_set_drvdata(pdev, i2c);
    i2c->adap = ocores_adapter;
    i2c_set_adapdata(&i2c->adap, i2c);
    i2c->adap.dev.parent = &pdev->dev;

    /* add i2c adapter to i2c tree */
    ret = i2c_add_adapter(&i2c->adap);
    if (ret)
        return ret;

    /* add in known devices to the bus */
    for (i = 0; i < pdata->num_devices; i++) {
        i2c_new_client_device(&i2c->adap, pdata->devices + i);
    }

    return 0;
}

static void ocores_i2c_remove(struct platform_device *pdev)
{
    struct ocores_i2c *i2c = platform_get_drvdata(pdev);
    u8 ctrl;

    LOCK(&cpld_access_lock);
    ctrl = oc_getreg(i2c, OCI2C_CONTROL);

    /* disable i2c logic */
    ctrl &= ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN);
    oc_setreg(i2c, OCI2C_CONTROL, ctrl);
    UNLOCK(&cpld_access_lock);

    /* remove adapter & data */
    i2c_del_adapter(&i2c->adap);
}

#ifdef CONFIG_PM_SLEEP
static int ocores_i2c_suspend(struct device *dev)
{
    struct ocores_i2c *i2c = dev_get_drvdata(dev);
    u8 ctrl;

    /* Disable I2C controller */
    LOCK(&cpld_access_lock);
    ctrl = oc_getreg(i2c, OCI2C_CONTROL);
    ctrl &= ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN);
    oc_setreg(i2c, OCI2C_CONTROL, ctrl);
    UNLOCK(&cpld_access_lock);

    return 0;
}

static int ocores_i2c_resume(struct device *dev)
{
    struct ocores_i2c *i2c = dev_get_drvdata(dev);

    return ocores_init(dev, i2c);
}

static SIMPLE_DEV_PM_OPS(ocores_i2c_pm, ocores_i2c_suspend, ocores_i2c_resume);
#define OCORES_I2C_PM (&ocores_i2c_pm)
#else
#define OCORES_I2C_PM NULL
#endif

static struct platform_driver ocores_i2c_driver = {
    .probe   = ocores_i2c_probe,
    .remove  = ocores_i2c_remove,
    .driver  = {
        .name = "ocores-as1813",
        .pm = OCORES_I2C_PM,
    },
};

static int __init ocores_i2c_as1813_128o_init(void)
{
    int err;

    spin_lock_init(&cpld_access_lock);

    err = platform_driver_register(&ocores_i2c_driver);
    if (err < 0) {
        pr_err("Failed to register ocores_i2c_driver");
        return err;
    }

    return 0;
}

static void __exit ocores_i2c_as1813_128o_exit(void)
{
    platform_driver_unregister(&ocores_i2c_driver);
}

module_init(ocores_i2c_as1813_128o_init);
module_exit(ocores_i2c_as1813_128o_exit);

MODULE_AUTHOR("Eric Yang <eric_yang@accton.com>");
MODULE_DESCRIPTION("OpenCores I2C bus driver for AS1813-128O");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ocores-as1813");

