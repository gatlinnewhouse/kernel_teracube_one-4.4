/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define LOG_TAG "LCM"

#ifdef BUILD_LK
#include <platform/upmu_common.h>
#include <platform/mt_gpio.h>
#include <platform/mt_i2c.h>
#include <platform/mt_pmic.h>
#include <string.h>
#else
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#endif
#include "lcm_drv.h"

#ifdef BUILD_LK
#define LCM_LOGI(string, args...)  dprintf(0, "[LK/"LOG_TAG"]"string, ##args)
#define LCM_LOGD(string, args...)  dprintf(1, "[LK/"LOG_TAG"]"string, ##args)
#else
#define LCM_LOGI(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#define LCM_LOGD(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#endif

#define LCM_ID_FT8719 (0x8719)

static const unsigned int BL_MIN_LEVEL = 20;
static LCM_UTIL_FUNCS lcm_util;

#define SET_RESET_PIN(v)	(lcm_util.set_reset_pin((v)))
#define MDELAY(n)		(lcm_util.mdelay(n))
#define UDELAY(n)		(lcm_util.udelay(n))

#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update) \
	lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd) lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums) \
	lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd) \
	lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size) \
	lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

/* static unsigned char lcd_id_pins_value = 0xFF; */
#define FRAME_WIDTH										(1080)
#define FRAME_HEIGHT									(2340)

/* physical size in um */
#define LCM_PHYSICAL_WIDTH									(69500)
#define LCM_PHYSICAL_HEIGHT									(150580)
#define LCM_DENSITY											(480)

#define REGFLAG_DELAY		    0xFFFC
#define REGFLAG_UDELAY	        0xFFFB
#define REGFLAG_END_OF_TABLE	0xFFFD
#define REGFLAG_RESET_LOW	    0xFFFE
#define REGFLAG_RESET_HIGH	    0xFFFF

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[64];
};

static struct LCM_setting_table lcm_suspend_setting[] = {
	{0x28, 0, {} },
	{REGFLAG_DELAY, 20, {} },
	{0x10, 0, {} },
	{REGFLAG_DELAY, 120, {} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

static struct LCM_setting_table lcm_init_setting[] = {
	//{0x01,0,{}},
	//{REGFLAG_DELAY,200, {}},
	{0x00,1,{0x00}},
	{0xFF,3,{0x87,0x19,0x01}},
	{0x00,1,{0x80}},
	{0xFF,2,{0x87,0x19}},
	{0x00,1,{0x82}},
	{0xA4,2,{0x29,0x23}},
	{0x00,1,{0x80}},
	{0xA7,1,{0x03}},
	{0x00,1,{0x82}},
	{0xA7,2,{0x11,0x01}},
	{0x00,1,{0x90}},
	{0xC3,4,{0x0C,0x00,0x00,0x01}},
	{0x00,1,{0xA0}},
	{0xC3,7,{0x31,0x21,0x02,0x10,0x01,0x20,0x12}},
	{0x00,1,{0xB3}},
	{0xC5,1,{0x08}},
	{0x00,1,{0x80}},
	{0xC2,4,{0x82,0x01,0x20,0x56}},
	{0x00,1,{0xA0}},
	{0xC2,15,{0x00,0x00,0x00,0x24,0x98,0x01,0x00,0x00,0x24,0x98,0x02,0x00,0x00,0x24,0x98}},
	{0x00,1,{0xB0}},
	{0xC2,10,{0x03,0x00,0x00,0x24,0x98,0x00,0x02,0x03,0x00,0x80}},
	{0x00,1,{0xE0}},
	{0xC2,8,{0x33,0x33,0x73,0x33,0x33,0x33,0x00,0x00}},
	{0x00,1,{0xFA}},
	{0xC2,3,{0x23,0xFF,0x23}},
	{0x00,1,{0x80}},
	{0xCB,16,{0x00,0x01,0x00,0x00,0xFD,0x01,0x00,0x00,0x00,0x00,0xFD,0x01,0x00,0x01,0x00,0x03}},
	{0x00,1,{0x90}},
	{0xCB,16,{0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00}},
	{0x00,1,{0xA0}},
	{0xCB,4,{0x00,0x00,0x00,0x00}},
	{0x00,1,{0xB0}},
	{0xCB,4,{0x55,0x55,0x55,0x57}},
	{0x00,1,{0x80}},
	{0xCC,16,{0x00,0x29,0x00,0x23,0x00,0x0A,0x00,0x00,0x09,0x08,0x07,0x06,0x00,0x00,0x00,0x00}},
	{0x00,1,{0x90}},
	{0xCC,8,{0x00,0x18,0x16,0x17,0x00,0x1C,0x1D,0x1E}},
	{0x00,1,{0x80}},
	{0xCD,16,{0x00,0x00,0x00,0x02,0x00,0x0A,0x00,0x00,0x09,0x08,0x07,0x06,0x00,0x00,0x00,0x00}},
	{0x00,1,{0x90}},
	{0xCD,8,{0x00,0x18,0x16,0x17,0x00,0x1C,0x1D,0x1E}},
	{0x00,1,{0xA0}},
	{0xCC,16,{0x00,0x29,0x00,0x23,0x00,0x0A,0x00,0x00,0x06,0x07,0x08,0x09,0x00,0x00,0x00,0x00}},
	{0x00,1,{0xB0}},
	{0xCC,8,{0x00,0x18,0x16,0x17,0x00,0x1C,0x1D,0x1E}},
	{0x00,1,{0xA0}},
	{0xCD,16,{0x00,0x00,0x00,0x02,0x00,0x0A,0x00,0x00,0x06,0x07,0x08,0x09,0x00,0x00,0x00,0x00}},
	{0x00,1,{0xB0}},
	{0xCD,8,{0x00,0x18,0x16,0x17,0x00,0x1C,0x1D,0x1E}},
	{0x00,1,{0x80}},
	{0xC0,6,{0x00,0x7A,0x00,0x6C,0x00,0x10}},
	{0x00,1,{0x89}},
	{0xC0,3,{0x01,0x1D,0x04}},
	{0x00,1,{0xA0}},
	{0xC0,6,{0x01,0x09,0x00,0x3A,0x00,0x10}},
	{0x00,1,{0xB0}},
	{0xC0,5,{0x00,0x7A,0x02,0x10,0x10}},
	{0x00,1,{0xC1}},
	{0xC0,8,{0x00,0xB1,0x00,0x8B,0x00,0x76,0x00,0xD0}},
	{0x00,1,{0xCA}},
	{0xC0,1,{0x80}},
	{0x00,1,{0xD7}},
	{0xC0,6,{0x00,0x76,0x00,0x6F,0x00,0x10}},
	{0x00,1,{0xA5}},
	{0xC1,4,{0x00,0x36,0x00,0x02}},
	{0x00,1,{0x82}},
	{0xCE,13,{0x01,0x09,0x00,0xD8,0x00,0xD8,0x00,0x90,0x00,0x90,0x0D,0x0E,0x09}},
	{0x00,1,{0x90}},
	{0xCE,8,{0x00,0x82,0x0D,0x5C,0x00,0x82,0x80,0x09}},
	{0x00,1,{0xA0}},
	{0xCE,3,{0x00,0x00,0x00}},
	{0x00,1,{0xB0}},
	{0xCE,3,{0x11,0x00,0x00}},
	{0x00,1,{0xD1}},
	{0xCE,7,{0x00,0x0A,0x01,0x01,0x00,0x5D,0x01}},
	{0x00,1,{0xE1}},
	{0xCE,11,{0x08,0x02,0x15,0x02,0x15,0x02,0x15,0x00,0x2B,0x00,0x60}},
	{0x00,1,{0xF1}},
	{0xCE,9,{0x16,0x0B,0x0F,0x01,0x12,0x01,0x11,0x01,0x23}},
	{0x00,1,{0xB0}},
	{0xCF,4,{0x00,0x00,0x6C,0x70}},
	{0x00,1,{0xB5}},
	{0xCF,4,{0x04,0x04,0xA4,0xA8}},
	{0x00,1,{0xC0}},
	{0xCF,4,{0x08,0x08,0xCA,0xCE}},
	{0x00,1,{0xC5}},
	{0xCF,4,{0x00,0x00,0x08,0x0C}},
	{0x00,1,{0x90}},
	{0xC0,6,{0x00,0x7A,0x00,0x6C,0x00,0x10}},
	{0x00,1,{0xA1}},
	{0xB3,6,{0x04,0x38,0x09,0x24,0xC0,0xF8}},
	{0x00,1,{0x82}},
	{0xC5,7,{0x4B,0x4B,0x3C,0x3C,0x00,0x60,0x0C}},
	{0x00,1,{0x00}},
	{0xD8,2,{0x2B,0x2B}},
	{0x00,1,{0x00}},
	{0xD9,3,{0x00,0x95,0x95}},
	{0x00,1,{0xA3}},
	{0xC5,1,{0x1B}},
	{0x00,1,{0xA9}},
	{0xC5,1,{0x21}},
	{0x00,1,{0x86}},
	{0xC3,3,{0x00,0x00,0x00}},
	{0x00,1,{0x89}},
	{0xF5,1,{0x5F}},
	{0x00,1,{0x96}},
	{0xF5,1,{0x5F}},
	{0x00,1,{0xA6}},
	{0xF5,1,{0x5F}},
	{0x00,1,{0xB1}},
	{0xF5,1,{0x1E}},
	{0x00,1,{0x81}},
	{0xF5,2,{0x5F,0x5F}},
	{0x00,1,{0x86}},
	{0xF5,2,{0x5F,0x5F}},
	{0x00,1,{0xAA}},
	{0xF5,1,{0x8E}},
	{0x00,1,{0x85}},
	{0xC4,1,{0x1E}},
	{0x00,1,{0xB7}},
	{0xCE,2,{0x2B,0x05}},
	{0x00,1,{0x90}},
	{0xC5,1,{0x83}},
	{0x00,1,{0x92}},
	{0xC5,1,{0x63}},
	{0x00,1,{0xE8}},
	{0xC0,1,{0x40}},
	{0x00,1,{0x87}},
	{0xC4,1,{0x40}},
	{0x00,1,{0x9B}},
	{0xF5,4,{0x8D,0x8C,0x8D,0x8A}},
	{0x00,1,{0x91}},
	{0xF5,2,{0xED,0x8C}},
	{0x00,1,{0x95}},
	{0xF5,1,{0x8A}},
	{0x00,1,{0x98}},
	{0xF5,1,{0xEB}},
	{0x00,1,{0x85}},
	{0xA7,1,{0x0F}},
	{0x00,1,{0x00}},
	{0xE1,40,{0x00,0x00,0x02,0x10,0x25,0x1D,0x26,0x2C,0x38,0x4C,0x3F,0x46,0x4C,0x52,0xBD,0x57,0x5F,0x66,0x6E,0x1C,0x74,0x7C,0x84,0x8C,0xD0,0x95,0x9C,0xA1,0xA8,0xCF,0xB1,0xBB,0xCA,0xD2,0x74,0xDC,0xEB,0xF5,0xFB,0xFB}},
	{0x00,1,{0x00}},
	{0xE2,40,{0x00,0x00,0x02,0x10,0x25,0x1D,0x26,0x2C,0x38,0x4C,0x41,0x48,0x4E,0x54,0x7D,0x59,0x61,0x68,0x70,0x1C,0x76,0x7C,0x84,0x8C,0xD0,0x95,0x9C,0xA1,0xA8,0xCF,0xB1,0xBB,0xC8,0xD1,0x77,0xDC,0xEB,0xF5,0xFB,0xFB}},
	{0x00,1,{0x00}},
	{0xE3,40,{0x00,0x00,0x02,0x10,0x25,0x1D,0x26,0x2C,0x38,0x4C,0x3F,0x46,0x4C,0x52,0xBD,0x57,0x5F,0x66,0x6E,0x1C,0x74,0x7C,0x84,0x8C,0xD0,0x95,0x9C,0xA1,0xA8,0xCF,0xB1,0xBB,0xCA,0xD2,0x74,0xDC,0xEB,0xF5,0xFB,0xFB}},
	{0x00,1,{0x00}},
	{0xE4,40,{0x00,0x00,0x02,0x10,0x25,0x1D,0x26,0x2C,0x38,0x4C,0x41,0x48,0x4E,0x54,0x7D,0x59,0x61,0x68,0x70,0x1C,0x76,0x7C,0x84,0x8C,0xD0,0x95,0x9A,0xA1,0xA8,0xE7,0xB1,0xBB,0xC8,0xD1,0x77,0xDC,0xEB,0xF5,0xFB,0xFB}},
	{0x00,1,{0x00}},
	{0xE5,40,{0x00,0x00,0x02,0x10,0x25,0x1D,0x26,0x2C,0x38,0x4C,0x3F,0x46,0x4C,0x52,0xBD,0x57,0x5F,0x66,0x6E,0x1C,0x74,0x7C,0x84,0x8C,0xD0,0x95,0x9C,0xA1,0xA8,0xCF,0xB1,0xBB,0xCA,0xD2,0x74,0xDC,0xEB,0xF5,0xFB,0xFB}},
	{0x00,1,{0x00}},
	{0xE6,40,{0x00,0x00,0x02,0x10,0x25,0x1D,0x26,0x2C,0x38,0x4C,0x41,0x48,0x4E,0x54,0x7D,0x59,0x61,0x68,0x70,0x1C,0x76,0x7C,0x84,0x8C,0xD0,0x95,0x9C,0xA1,0xA8,0xCF,0xB1,0xBB,0xC8,0xD1,0x77,0xDC,0xEB,0xF5,0xFB,0xFB}},
	{0x00,1,{0xB0}},
	{0xF5,1,{0x00}},
	{0x00,1,{0xC1}},
	{0xB6,3,{0x09,0x89,0x68}},
	{0x00,1,{0x80}},
	{0xB4,1,{0x0A}},
	{0x00,1,{0x8C}},
	{0xC3,1,{0x01}},
	{0x00,1,{0x8E}},
	{0xC3,1,{0x10}},
	{0x00,1,{0x8A}},
	{0xC0,2,{0x1C,0x05}},
	{0x00,1,{0xB0}},
	{0xF3,2,{0x02,0xFD}},
	{0x11,0,{}},
	{REGFLAG_DELAY,120, {}},
	{0x29,0,{}},
	{REGFLAG_DELAY,20, {}},
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

static void push_table(void *cmdq, struct LCM_setting_table *table,
		unsigned int count, unsigned char force_update)
{
	unsigned int i;
	unsigned cmd;

	for (i = 0; i < count; i++) {
		cmd = table[i].cmd;

		switch (cmd) {
			case REGFLAG_DELAY:
				if (table[i].count <= 10)
					MDELAY(table[i].count);
				else
					MDELAY(table[i].count);
				break;
			case REGFLAG_UDELAY:
				UDELAY(table[i].count);
				break;
			case REGFLAG_END_OF_TABLE:
				break;
			default:
				dsi_set_cmdq_V22(cmdq, cmd, table[i].count, table[i].para_list, force_update);
		}
	}
}

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}

static void lcm_get_params(LCM_PARAMS *params)
{
	memset(params, 0, sizeof(LCM_PARAMS));

	params->type = LCM_TYPE_DSI;
	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;

	params->physical_width = LCM_PHYSICAL_WIDTH/1000;
	params->physical_height = LCM_PHYSICAL_HEIGHT/1000;
	params->physical_width_um = LCM_PHYSICAL_WIDTH;
	params->physical_height_um = LCM_PHYSICAL_HEIGHT;
	params->density            = LCM_DENSITY;

	params->dsi.mode = SYNC_PULSE_VDO_MODE;
	params->dsi.switch_mode = CMD_MODE;
	params->dsi.switch_mode_enable = 0;

	/* DSI */
	/* Command mode setting */
	params->dsi.LANE_NUM = LCM_FOUR_LANE;
	/* The following defined the fomat for data coming from LCD engine. */
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;

	/* Highly depends on LCD driver capability. */
	params->dsi.packet_size = 256;
	/* video mode timing */

	params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

	params->dsi.vertical_sync_active = 4;
	params->dsi.vertical_backporch = 26;
	params->dsi.vertical_frontporch = 112;
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active = 6;
	params->dsi.horizontal_backporch = 16;
	params->dsi.horizontal_frontporch = 16;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;

	params->dsi.PLL_CLOCK = 502;
	params->dsi.ssc_disable = 1;
	params->dsi.clk_lp_per_line_enable = 0;
	params->dsi.esd_check_enable = 0;
	params->dsi.customization_esd_check_enable = 0;
	params->dsi.lcm_esd_check_table[0].cmd = 0x0A;
	params->dsi.lcm_esd_check_table[0].count = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9C;
}

static void lcm_init_power(void)
{
	display_bias_enable();
}

static void lcm_suspend_power(void)
{
	display_bias_disable();
}

static void lcm_resume_power(void)
{
	SET_RESET_PIN(0);
	display_bias_enable();
}

static void lcm_init(void)
{
	SET_RESET_PIN(1);
	MDELAY(10);
	SET_RESET_PIN(0);
	MDELAY(10);
	SET_RESET_PIN(1);
	MDELAY(120);
	LCM_LOGD("ft8719 lcm_init\n");
	push_table(NULL, lcm_init_setting, sizeof(lcm_init_setting) / sizeof(struct LCM_setting_table), 1);
}

static void lcm_suspend(void)
{
	push_table(NULL, lcm_suspend_setting, sizeof(lcm_suspend_setting) / sizeof(struct LCM_setting_table), 1);
	MDELAY(10);
	/* SET_RESET_PIN(0); */
}

static void lcm_resume(void)
{
	lcm_init();
}

static unsigned int lcm_compare_id(void)
{
	unsigned int manufacture_id = 0,version_id = 0,id = 0;
	unsigned char buffer[4];
	unsigned int array[16];
	unsigned int lcm_id = 0;

	SET_RESET_PIN(1);
	MDELAY(20);
	SET_RESET_PIN(0);
	MDELAY(20);
	SET_RESET_PIN(1);
	MDELAY(50);

	array[0]=0x00023902;
	array[1]=0x00000000;
	dsi_set_cmdq(array, 2, 1);
	array[0]=0x00043902;
	array[1]=0x011987FF;
	dsi_set_cmdq(array, 2, 1);
	array[0]=0x00023902;
	array[1]=0x00008000;
	dsi_set_cmdq(array, 2, 1);
	array[0]=0x00033902;
	array[1]=0x001987FF;
	dsi_set_cmdq(array, 2, 1);

	array[0] = 0x00043700;	/* read id return four bytes of data */
	dsi_set_cmdq(array, 1, 1);
	read_reg_v2(0xA1, buffer, 4);

	lcm_id = (buffer[2] << 8) | buffer[3];
#ifdef BUILD_LK
	printf("ft8719_jdi653_hst_fhdpp2340_lcm_drv:lcm_id = 0x%x\n", lcm_id);
#else
	printk("ft8719_jdi653_hst_fhdpp2340_lcm_drv:lcm_id = 0x%x\n", lcm_id);
#endif

	return ((lcm_id == LCM_ID_FT8719) ? 1 : 0);
}

LCM_DRIVER ft8719_jdi653_hst_fhdpp2340_lcm_drv = {
	.name = "ft8719_jdi653_hst_fhdpp2340_lcm_drv",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
	//.compare_id = lcm_compare_id,
	.init_power = lcm_init_power,
	.resume_power = lcm_resume_power,
	.suspend_power = lcm_suspend_power,
};
