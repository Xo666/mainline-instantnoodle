// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct samsung_amb655uv01 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	bool first_prepare;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[3];
};

static inline
struct samsung_amb655uv01 *to_samsung_amb655uv01(struct drm_panel *panel)
{
	return container_of(panel, struct samsung_amb655uv01, panel);
}

static void samsung_amb655uv01_reset(struct samsung_amb655uv01 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int samsung_amb655uv01_on(struct samsung_amb655uv01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9d, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9e,
				     0x11, 0x00, 0x00, 0x89, 0x30, 0x80, 0x09,
				     0x60, 0x04, 0x38, 0x00, 0x30, 0x02, 0x1c,
				     0x02, 0x1c, 0x02, 0x00, 0x02, 0x52, 0x00,
				     0x20, 0x04, 0x23, 0x00, 0x07, 0x00, 0x0f,
				     0x02, 0x8e, 0x02, 0x1f, 0x18, 0x00, 0x10,
				     0xf0, 0x03, 0x0c, 0x20, 0x00, 0x06, 0x0b,
				     0x0b, 0x33, 0x0e, 0x1c, 0x2a, 0x38, 0x46,
				     0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b,
				     0x7d, 0x7e, 0x01, 0x02, 0x01, 0x00, 0x09,
				     0x40, 0x09, 0xbe, 0x19, 0xfc, 0x19, 0xfa,
				     0x19, 0xf8, 0x1a, 0x38, 0x1a, 0x78, 0x1a,
				     0xb6, 0x2a, 0xb6, 0x2a, 0xf4, 0x2a, 0xf4,
				     0x4b, 0x34, 0x63, 0x74);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5); //KEY DISABLE
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a); //KEY ENABLE
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf5, 0x85);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf5, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf5, 0x10, 0x40, 0x10, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x3b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf5, 0x25);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5); //DISABLE - Next 2 are written in disabled
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0x0000, 0x0437);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x095f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a); //ENABLE
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd7, 0x07, 0x02, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf5, 0x85);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf8, 0xc4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf8,
				     0x41, 0x47, 0x11, 0xb8, 0xb8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf8, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf2, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5); //DISABLED
	mipi_dsi_msleep(&dsi_ctx, 110); //Sleep for 110ms
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a); //ENABLED
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf5, 0x87);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x01, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5); //DISABLED
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a); //RE_ENABLED
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5); //Disable Key
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx); //Turn on screen

	return dsi_ctx.accum_err;
}

static int samsung_amb655uv01_off(struct samsung_amb655uv01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int samsung_amb655uv01_prepare(struct drm_panel *panel)
{
	struct samsung_amb655uv01 *ctx = to_samsung_amb655uv01(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	samsung_amb655uv01_reset(ctx);

	ret = samsung_amb655uv01_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);

	ret = mipi_dsi_picture_parameter_set(ctx->dsi, &pps);
	if (ret < 0) {
		dev_err(panel->dev, "failed to transmit PPS: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_compression_mode(ctx->dsi, true);
	if (ret < 0) {
		dev_err(dev, "failed to enable compression mode: %d\n", ret);
		return ret;
	}

	msleep(28); /* TODO: Is this panel-dependent? */

	return 0;
}

static int samsung_amb655uv01_unprepare(struct drm_panel *panel)
{
	struct samsung_amb655uv01 *ctx = to_samsung_amb655uv01(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = samsung_amb655uv01_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode samsung_amb655uv01_modes[] = {
	{ /* 90Hz Mode */
		.clock = (1080 + 8 + 24 + 8) * (2400 + 4 + 4 + 6) * 90 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 8,
		.hsync_end = 1080 + 8 + 24,
		.htotal = 1080 + 8 + 24 + 8,
		.vdisplay = 2400,
		.vsync_start = 2400 + 4,
		.vsync_end = 2400 + 4 + 4,
		.vtotal = 2400 + 4 + 4 + 6,
		.width_mm = 70,
		.height_mm = 151,
		.type = DRM_MODE_TYPE_DRIVER,
	},
	{
		.clock = (1080 + 8 + 24 + 8) * (2400 + 4 + 4 + 6) * 60 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 8,
		.hsync_end = 1080 + 8 + 24,
		.htotal = 1080 + 8 + 24 + 8,
		.vdisplay = 2400,
		.vsync_start = 2400 + 4,
		.vsync_end = 2400 + 4 + 4,
		.vtotal = 2400 + 4 + 4 + 6,
		.width_mm = 70,
		.height_mm = 151,
		.type = DRM_MODE_TYPE_DRIVER,
	},
};

static int samsung_amb655uv01_get_modes(struct drm_panel *panel,
				      struct drm_connector *connector)
{
	int count = 0;

	for (int i = 0; i < ARRAY_SIZE(samsung_amb655uv01_modes); i++) {
		count += drm_connector_helper_get_modes_fixed(connector,
						    &samsung_amb655uv01_modes[i]);
	}

	return count;
}

static const struct drm_panel_funcs samsung_amb655uv01_panel_funcs = {
	.prepare = samsung_amb655uv01_prepare,
	.unprepare = samsung_amb655uv01_unprepare,
	.get_modes = samsung_amb655uv01_get_modes,
};

static int samsung_amb655uv01_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int samsung_amb655uv01_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops samsung_amb655uv01_bl_ops = {
	.update_status = samsung_amb655uv01_bl_update_status,
	.get_brightness = samsung_amb655uv01_bl_get_brightness,
};

static struct backlight_device *
samsung_amb655uv01_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 1048,
		.max_brightness = 1740, //15% reduction to prevent burn-in, original value is 2047
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &samsung_amb655uv01_bl_ops, &props);
}

static int samsung_amb655uv01_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct samsung_amb655uv01 *ctx;
	int ret;

	pr_info("samsung_amb655uv01_probe\n");

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "vdd";
	ctx->supplies[2].supply = "avdd";

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to get vddio regulator\n");

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Failed to enable regulators\n");

	ctx->first_prepare = true;

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;

	drm_panel_init(&ctx->panel, dev, &samsung_amb655uv01_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->panel.backlight = samsung_amb655uv01_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");
	ctx->panel.prepare_prev_first = true;

	drm_panel_add(&ctx->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 1;

	/* TODO: Pass slice_per_pkt = 2 */
	ctx->dsc.slice_height = 48;
	ctx->dsc.slice_width = 540;
	/*
	 * TODO: hdisplay should be read from the selected mode once
	 * it is passed back to drm_panel (in prepare?)
	 */
	WARN_ON(1080 % ctx->dsc.slice_width);
	ctx->dsc.slice_count = 1080 / ctx->dsc.slice_width;
	ctx->dsc.bits_per_component = 8;
	ctx->dsc.bits_per_pixel = 8 << 4; /* 4 fractional bits */
	ctx->dsc.block_pred_enable = true;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void samsung_amb655uv01_remove(struct mipi_dsi_device *dsi)
{
	struct samsung_amb655uv01 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id samsung_amb655uv01_of_match[] = {
	{ .compatible = "samsung,amb655uv01" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, samsung_amb655uv01_of_match);

static struct mipi_dsi_driver samsung_amb655uv01_driver = {
	.probe = samsung_amb655uv01_probe,
	.remove = samsung_amb655uv01_remove,
	.driver = {
		.name = "panel-samsung-amb655uv01",
		.of_match_table = samsung_amb655uv01_of_match,
	},
};
module_mipi_dsi_driver(samsung_amb655uv01_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for Samsung amb655uv01 panel");
MODULE_LICENSE("GPL");
