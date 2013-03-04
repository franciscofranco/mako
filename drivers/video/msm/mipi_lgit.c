/*
 *  Copyright (C) 2011-2012, LG Eletronics,Inc. All rights reserved.
 *      LGIT LCD device driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#include <linux/gpio.h>
#include <linux/syscore_ops.h>

#include "msm_fb.h"
#include "mipi_dsi.h"
#include "mipi_lgit.h"
#include "mdp4.h"

static struct msm_panel_common_pdata *mipi_lgit_pdata;

static struct dsi_buf lgit_tx_buf;
static struct dsi_buf lgit_rx_buf;
static int skip_init;

#ifdef CONFIG_GAMMA_CONTROL
static DEFINE_MUTEX(color_lock);
struct dsi_cmd_desc new_color_vals[33];
int get_whites(void);
int get_mids(void);
int get_blacks(void);
int get_contrast(void);
int get_brightness(void);
int get_saturation(void);
int get_greys(void);
#endif

int kcal_refresh_values(void);

#define DSV_ONBST 57

static int lgit_external_dsv_onoff(uint8_t on_off)
{
	int ret =0;
	static int init_done=0;

	if (!init_done) {
		ret = gpio_request(DSV_ONBST,"DSV_ONBST_en");
		if (ret) {
			pr_err("%s: failed to request DSV_ONBST gpio \n", __func__);
			goto out;
		}
		ret = gpio_direction_output(DSV_ONBST, 1);
		if (ret) {
			pr_err("%s: failed to set DSV_ONBST direction\n", __func__);
			goto err_gpio;
		}
		init_done = 1;
	}

	gpio_set_value(DSV_ONBST, on_off);
	mdelay(20);
	goto out;

err_gpio:
	gpio_free(DSV_ONBST);
out:
	return ret;
}

static int mipi_lgit_lcd_on(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	int ret = 0;

	pr_info("%s started\n", __func__);

	mfd = platform_get_drvdata(pdev);
	if (!mfd)
		return -ENODEV;
	if (mfd->key != MFD_KEY)
		return -EINVAL;

	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x10000000);
	ret = mipi_dsi_cmds_tx(&lgit_tx_buf,
#ifdef CONFIG_GAMMA_CONTROL
			new_color_vals,
#else
			mipi_lgit_pdata->power_on_set_1,
#endif
			mipi_lgit_pdata->power_on_set_size_1);
	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x14000000);
	if (ret < 0) {
		pr_err("%s: failed to transmit power_on_set_1 cmds\n", __func__);
		return ret;
	}

	if(!skip_init){
		MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x10000000);
		ret = mipi_dsi_cmds_tx(&lgit_tx_buf,
				mipi_lgit_pdata->power_on_set_2,
				mipi_lgit_pdata->power_on_set_size_2);
		MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x14000000);
		if (ret < 0) {
			pr_err("%s: failed to transmit power_on_set_2 cmds\n", __func__);
			return ret;
		}
	}
	skip_init = false;

	ret = lgit_external_dsv_onoff(1);
	if (ret < 0) {
		pr_err("%s: failed to turn on external dsv\n", __func__);
		return ret;
	}

	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x10000000);
	ret = mipi_dsi_cmds_tx(&lgit_tx_buf,
			mipi_lgit_pdata->power_on_set_3,
			mipi_lgit_pdata->power_on_set_size_3);
	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x14000000);
	if (ret < 0) {
		pr_err("%s: failed to transmit power_on_set_3 cmds\n", __func__);
		return ret;
	}

	kcal_refresh_values();
	pr_info("%s finished\n", __func__);
	return 0;
}

static int mipi_lgit_lcd_off(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	int ret = 0;

	pr_info("%s started\n", __func__);

	if (mipi_lgit_pdata->bl_pwm_disable)
		mipi_lgit_pdata->bl_pwm_disable();

	mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x10000000);
	ret = mipi_dsi_cmds_tx(&lgit_tx_buf,
			mipi_lgit_pdata->power_off_set_1,
			mipi_lgit_pdata->power_off_set_size_1);
	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x14000000);
	if (ret < 0) {
		pr_err("%s: failed to transmit power_off_set_1 cmds\n", __func__);
		return ret;
	}

	ret = lgit_external_dsv_onoff(0);
	if (ret < 0) {
		pr_err("%s: failed to turn off external dsv\n", __func__);
		return ret;
	}

	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x10000000);
	ret = mipi_dsi_cmds_tx(&lgit_tx_buf,
			mipi_lgit_pdata->power_off_set_2,
			mipi_lgit_pdata->power_off_set_size_2);
	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x14000000);
	if (ret < 0) {
		pr_err("%s: failed to transmit power_off_set_2 cmds\n", __func__);
		return ret;
	}

	pr_info("%s finished\n", __func__);
	return 0;
}

static void mipi_lgit_lcd_shutdown(void)
{
	int ret = 0;

	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x10000000);
	ret = mipi_dsi_cmds_tx(&lgit_tx_buf,
			mipi_lgit_pdata->power_off_set_1,
			mipi_lgit_pdata->power_off_set_size_1);
	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x14000000);
	if (ret < 0) {
		pr_err("%s: failed to transmit power_off_set_1 cmds\n", __func__);
	}

	ret = lgit_external_dsv_onoff(0);
	if (ret < 0) {
		pr_err("%s: failed to turn off external dsv\n", __func__);
	}
	mdelay(20);

	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x10000000);
	ret = mipi_dsi_cmds_tx(&lgit_tx_buf,
			mipi_lgit_pdata->power_off_set_2,
			mipi_lgit_pdata->power_off_set_size_2);
	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x14000000);
	if (ret < 0) {
		pr_err("%s: failed to transmit power_off_set_2 cmds\n", __func__);
	}

	pr_info("%s finished\n", __func__);
}

static int mipi_lgit_backlight_on_status(void)
{
	return (mipi_lgit_pdata->bl_on_status());
}

static void mipi_lgit_set_backlight_board(struct msm_fb_data_type *mfd)
{
	int level;

	level = (int)mfd->bl_level;
	mipi_lgit_pdata->backlight_level(level, 0, 0);
}

#ifdef CONFIG_GAMMA_CONTROL
void update_vals(int array_pos)
{
	int val = 0;
	int ret = 0;
	int i;

	switch(array_pos) {
		case 1:
			val = get_greys();
			break;
		case 2:
			val = get_mids();
			break;
		case 3:
			val = get_blacks();
			break;
		case 5:
			val = get_contrast();
			break;
		case 6:
			val = get_brightness();
			break;
		case 7:
			val = get_saturation();
			break;
		case 8:
			val = get_whites();
			break;
		default:
			pr_info("%s - Wrong value - abort.\n", __FUNCTION__);
			return;
	}
	
	for (i = 5; i <= 10; i++)
		new_color_vals[i].payload[array_pos] = val;

	pr_info("%s - Updating display GAMMA settings.\n", __FUNCTION__);
	
	mutex_lock(&color_lock);

	//blocks the CPU so it doesn't end up in a race condition and panics the system
	mdelay(20);
	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x10000000);
	ret = mipi_dsi_cmds_tx(&lgit_tx_buf,
			new_color_vals,
			mipi_lgit_pdata->power_on_set_size_1);
	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x14000000);
	if (ret < 0)
		pr_err("%s: failed to transmit power_on_set_1 cmds\n", __func__);
	mutex_unlock(&color_lock);
}
EXPORT_SYMBOL(update_vals);
#endif

struct syscore_ops panel_syscore_ops = {
	.shutdown = mipi_lgit_lcd_shutdown,
};

static int mipi_lgit_lcd_probe(struct platform_device *pdev)
{
	if (pdev->id == 0) {
		mipi_lgit_pdata = pdev->dev.platform_data;
		return 0;
	}
	
#ifdef CONFIG_GAMMA_CONTROL
	memcpy((void *) new_color_vals, (void *) mipi_lgit_pdata->power_on_set_1, sizeof(new_color_vals));
#endif

	pr_info("%s start\n", __func__);

	skip_init = true;
	msm_fb_add_device(pdev);

	register_syscore_ops(&panel_syscore_ops);

	return 0;
}

static struct platform_driver this_driver = {
	.probe = mipi_lgit_lcd_probe,
	.driver = {
		.name = "mipi_lgit",
	},
};

static struct msm_fb_panel_data lgit_panel_data = {
	.on = mipi_lgit_lcd_on,
	.off = mipi_lgit_lcd_off,
	.set_backlight = mipi_lgit_set_backlight_board,
	.get_backlight_on_status = mipi_lgit_backlight_on_status,
};

static int ch_used[3];

int mipi_lgit_device_register(struct msm_panel_info *pinfo,
		u32 channel, u32 panel)
{
	struct platform_device *pdev = NULL;
	int ret;

	if ((channel >= 3) || ch_used[channel])
		return -ENODEV;

	ch_used[channel] = TRUE;

	pdev = platform_device_alloc("mipi_lgit", (panel << 8)|channel);
	if (!pdev)
		return -ENOMEM;

	lgit_panel_data.panel_info = *pinfo;

	ret = platform_device_add_data(pdev, &lgit_panel_data,
			sizeof(lgit_panel_data));
	if (ret) {
		pr_err("%s: platform_device_add_data failed!\n", __func__);
		goto err_device_put;
	}

	ret = platform_device_add(pdev);
	if (ret) {
		pr_err("%s: platform_device_register failed!\n", __func__);
		goto err_device_put;
	}
	return 0;

err_device_put:
	platform_device_put(pdev);
	return ret;
}

static int __init mipi_lgit_lcd_init(void)
{
	mipi_dsi_buf_alloc(&lgit_tx_buf, DSI_BUF_SIZE);
	mipi_dsi_buf_alloc(&lgit_rx_buf, DSI_BUF_SIZE);

	return platform_driver_register(&this_driver);
}

module_init(mipi_lgit_lcd_init);
