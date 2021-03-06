// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2014 Eric Bénard <eric@eukrea.com>
// SPDX-FileCopyrightText: 2013 Lucas Stach <l.stach@pengutronix.de>

#include <asm/armlinux.h>
#include <asm/io.h>
#include <bootsource.h>
#include <common.h>
#include <environment.h>
#include <envfs.h>
#include <gpio.h>
#include <init.h>
#include <mach/generic.h>
#include <mach/imx6-regs.h>
#include <mach/imx6.h>
#include <mach/bbu.h>
#include <mfd/imx6q-iomuxc-gpr.h>
#include <linux/sizes.h>
#include <linux/phy.h>

static int ar8035_phy_fixup(struct phy_device *dev)
{
	u16 val;

	/* Ar803x phy SmartEEE feature cause link status generates glitch,
	 * which cause ethernet link down/up issue, so disable SmartEEE
	 */
	phy_write(dev, 0xd, 0x3);
	phy_write(dev, 0xe, 0x805d);
	phy_write(dev, 0xd, 0x4003);

	val = phy_read(dev, 0xe);
	phy_write(dev, 0xe, val & ~(1 << 8));

	/* To enable AR8031 ouput a 125MHz clk from CLK_25M */
	phy_write(dev, 0xd, 0x7);
	phy_write(dev, 0xe, 0x8016);
	phy_write(dev, 0xd, 0x4007);

	val = phy_read(dev, 0xe);
	val &= 0xffe3;
	val |= 0x18;
	phy_write(dev, 0xe, val);

	/* introduce tx clock delay */
	phy_write(dev, 0x1d, 0x5);
	val = phy_read(dev, 0x1e);
	val |= 0x0100;
	phy_write(dev, 0x1e, val);

	return 0;
}

static int riotboard_device_init(void)
{
	if (!of_machine_is_compatible("riot,imx6s-riotboard"))
		return 0;

	phy_register_fixup_for_uid(0x004dd072, 0xffffffef, ar8035_phy_fixup);

	imx6_bbu_internal_mmc_register_handler("emmc", "/dev/mmc3.barebox",
			BBU_HANDLER_FLAG_DEFAULT);

	barebox_set_hostname("riotboard");

	return 0;
}
device_initcall(riotboard_device_init);
