// SPDX-License-Identifier: GPL-2.0-only
/*
 * A V4L2 driver for Sony imx481 cameras.
 * Copyright (c) 2025, Eugene Lepshy <fekz115@gmail.com>
 * Copyright (C) 2024 Luca Weiss <luca.weiss@fairphone.com>
 *
 * Based on Sony imx412 camera driver
 * Copyright (C) 2021 Intel Corporation
 */
#include <linux/unaligned.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* Streaming Mode */
#define imx481_REG_MODE_SELECT	0x0100
#define imx481_MODE_STANDBY	0x00
#define imx481_MODE_STREAMING	0x01

/* Lines per frame */
#define imx481_REG_LPFR		0x0340

/* Chip ID */
#define imx481_REG_ID		0x0016
#define imx481_ID		0x0481

/* Exposure control */
#define imx481_REG_EXPOSURE_CIT	0x0202
#define imx481_EXPOSURE_MIN	8
#define imx481_EXPOSURE_OFFSET	22
#define imx481_EXPOSURE_STEP	1
#define imx481_EXPOSURE_DEFAULT	0x0648

/* Analog gain control */
#define imx481_REG_AGAIN	0x0204
#define imx481_AGAIN_MIN	0
#define imx481_AGAIN_MAX	978
#define imx481_AGAIN_STEP	1
#define imx481_AGAIN_DEFAULT	0

/* Group hold register */
#define imx481_REG_HOLD		0x0104

/* Input clock rate */
#define imx481_INCLK_RATE_19Mhz	19200000
#define imx481_INCLK_RATE_24Mhz	24000000

/* CSI2 HW configuration */
#define imx481_LINK_FREQ	600000000
#define imx481_NUM_DATA_LANES	4

#define imx481_REG_MIN		0x00
#define imx481_REG_MAX		0xffff

/**
 * struct imx481_reg - imx481 sensor register
 * @address: Register address
 * @val: Register value
 */
struct imx481_reg {
	u16 address;
	u8 val;
};

/**
 * struct imx481_reg_list - imx481 sensor register list
 * @num_of_regs: Number of registers in the list
 * @regs: Pointer to register list
 */
struct imx481_reg_list {
	u32 num_of_regs;
	const struct imx481_reg *regs;
};

static const int imx481_inclk_list[] = {
	imx481_INCLK_RATE_19Mhz,
	imx481_INCLK_RATE_24Mhz,
};

/**
 * struct imx481_mode - imx481 sensor mode structure
 * @width: Frame width
 * @height: Frame height
 * @code: Format code
 * @hblank: Horizontal blanking in lines
 * @vblank: Vertical blanking in lines
 * @vblank_min: Minimum vertical blanking in lines
 * @vblank_max: Maximum vertical blanking in lines
 * @pclk: Sensor pixel clock
 * @link_freq_idx: Link frequency index
 * @reg_list: Register list for sensor mode
 */
struct imx481_mode {
	u32 width;
	u32 height;
	u32 code;
	u32 hblank;
	u32 vblank;
	u32 vblank_min;
	u32 vblank_max;
	u64 pclk;
	u32 link_freq_idx;
	struct imx481_reg_list reg_list;
};

static const char * const imx481_supply_names[] = {
	"vio",	/* 1.8V I/O Power */
	"vdig",		/* 1.05V Digital Power */
	"vana",		/* 2.8V Analog Power */
};

/**
 * struct imx481 - imx481 sensor device structure
 * @dev: Pointer to generic device
 * @client: Pointer to i2c client
 * @sd: V4L2 sub-device
 * @pad: Media pad. Only one pad supported
 * @reset_gpio: Sensor reset gpio
 * @inclk: Sensor input clock
 * @supplies: Regulator supplies
 * @ctrl_handler: V4L2 control handler
 * @link_freq_ctrl: Pointer to link frequency control
 * @pclk_ctrl: Pointer to pixel clock control
 * @hblank_ctrl: Pointer to horizontal blanking control
 * @vblank_ctrl: Pointer to vertical blanking control
 * @exp_ctrl: Pointer to exposure control
 * @again_ctrl: Pointer to analog gain control
 * @vblank: Vertical blanking in lines
 * @cur_mode: Pointer to current selected sensor mode
 * @mutex: Mutex for serializing sensor controls
 */
struct imx481 {
	struct device *dev;
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct gpio_desc *reset_gpio;
	struct clk *inclk;
	struct regulator_bulk_data supplies[ARRAY_SIZE(imx481_supply_names)];
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq_ctrl;
	struct v4l2_ctrl *pclk_ctrl;
	struct v4l2_ctrl *hblank_ctrl;
	struct v4l2_ctrl *vblank_ctrl;
	struct {
		struct v4l2_ctrl *exp_ctrl;
		struct v4l2_ctrl *again_ctrl;
	};
	u32 vblank;
	const struct imx481_mode *cur_mode;
	struct mutex mutex;
};

static const s64 link_freq[] = {
	imx481_LINK_FREQ,
};

/* Sensor mode registers */
static const struct imx481_reg mode_2304x1728_regs[] = {
    {0x0136, 0x13},
    {0x0137, 0x33},
    {0x3C7E, 0x02},
    {0x3C7F, 0x06},
    {0x3F7F, 0x01},
    {0x531C, 0x01},
    {0x531D, 0x02},
    {0x531E, 0x04},
    {0x5928, 0x00},
    {0x5929, 0x2F},
    {0x592A, 0x00},
    {0x592B, 0x85},
    {0x592C, 0x00},
    {0x592D, 0x32},
    {0x592E, 0x00},
    {0x592F, 0x88},
    {0x5930, 0x00},
    {0x5931, 0x3D},
    {0x5932, 0x00},
    {0x5933, 0x93},
    {0x5938, 0x00},
    {0x5939, 0x24},
    {0x593A, 0x00},
    {0x593B, 0x7A},
    {0x593C, 0x00},
    {0x593D, 0x24},
    {0x593E, 0x00},
    {0x593F, 0x7A},
    {0x5940, 0x00},
    {0x5941, 0x2F},
    {0x5942, 0x00},
    {0x5943, 0x85},
    {0x5E12, 0x00},
    {0x5E13, 0x23},
    {0x5F06, 0x08},
    {0x5F07, 0x81},
    {0x5F0B, 0xEB},
    {0x5F0C, 0xAE},
    {0x5F0D, 0x15},
    {0x5F0E, 0x6E},
    {0x5F0F, 0x03},
    {0x5F10, 0xA5},
    {0x5F11, 0xC6},
    {0x5F12, 0x92},
    {0x5F13, 0xB9},
    {0x5F14, 0x5E},
    {0x5F17, 0x5E},
    {0x5F18, 0xDC},
    {0x5F19, 0x23},
    {0x5F1A, 0xDB},
    {0x5F1B, 0xC7},
    {0x5F1C, 0x5B},
    {0x5F1D, 0x7E},
    {0x5F1E, 0x20},
    {0x5F1F, 0x51},
    {0x5F20, 0xA2},
    {0x5F21, 0x46},
    {0x5F22, 0x87},
    {0x5F23, 0x2C},
    {0x5F24, 0x1D},
    {0x5F25, 0x10},
    {0x5F26, 0x76},
    {0x5F27, 0xA1},
    {0x5F28, 0xC6},
    {0x5F29, 0x07},
    {0x5F2A, 0x1A},
    {0x5F2B, 0x1C},
    {0x5F2C, 0xA8},
    {0x5F2D, 0x76},
    {0x5F2E, 0x61},
    {0x5F2F, 0xC6},
    {0x5F30, 0x87},
    {0x5F31, 0x2C},
    {0x5F32, 0x1D},
    {0x5F33, 0x10},
    {0x5F34, 0x76},
    {0x5F35, 0xA1},
    {0x5F36, 0xC6},
    {0x5F37, 0x07},
    {0x5F38, 0x1A},
    {0x5F39, 0x1C},
    {0x5F3A, 0xA8},
    {0x5F3B, 0x76},
    {0x5F3C, 0xA1},
    {0x5F3D, 0xC6},
    {0x5F3E, 0x87},
    {0x5F3F, 0x2C},
    {0x5F40, 0x1D},
    {0x5F41, 0x10},
    {0x5F42, 0x76},
    {0x5F43, 0xA1},
    {0x5F44, 0xC6},
    {0x5F45, 0x07},
    {0x5F46, 0x2A},
    {0x5F47, 0x1D},
    {0x5F48, 0x08},
    {0x5F49, 0x76},
    {0x5F4A, 0x81},
    {0x5F4B, 0xC0},
    {0x5F75, 0x27},
    {0x5F76, 0xEE},
    {0x5F77, 0xEE},
    {0x5F78, 0xEE},
    {0x5F79, 0xE5},
    {0x7990, 0x01},
    {0x7993, 0x5D},
    {0x7994, 0x5D},
    {0x7995, 0xA1},
    {0x799A, 0x01},
    {0x799D, 0x00},
    {0x8169, 0x01},
    {0x8359, 0x01},
    {0x88C7, 0x00},
    {0x88D4, 0x03},
    {0x9300, 0x2A},
    {0x9301, 0x24},
    {0x9302, 0x1E},
    {0x9304, 0x2C},
    {0x9305, 0x23},
    {0x9306, 0x1F},
    {0x9308, 0x2D},
    {0x9309, 0x28},
    {0x930A, 0x26},
    {0x930C, 0x2E},
    {0x930D, 0x2C},
    {0x930E, 0x23},
    {0x9310, 0x2E},
    {0x9311, 0x28},
    {0x9312, 0x23},
    {0x9314, 0x31},
    {0x9315, 0x31},
    {0x9316, 0x2C},
    {0x9317, 0x19},
    {0x9960, 0x00},
    {0x9963, 0x64},
    {0x9964, 0x50},
    {0xA391, 0x04},
    {0xB046, 0x01},
    {0xB048, 0x01},
    {0x0112, 0x0A},
    {0x0113, 0x0A},
    {0x0114, 0x03},
    {0x0342, 0x14},
    {0x0343, 0x00},
    {0x0340, 0x0D},
    {0x0341, 0xEA},
    {0x0344, 0x00},
    {0x0345, 0x00},
    {0x0346, 0x00},
    {0x0347, 0x00},
    {0x0348, 0x12},
    {0x0349, 0x2F},
    {0x034A, 0x0D},
    {0x034B, 0xA7},
    {0x0381, 0x01},
    {0x0383, 0x01},
    {0x0385, 0x01},
    {0x0387, 0x01},
    {0x0900, 0x00},
    {0x0901, 0x00},
    {0x0902, 0x00},
    {0x3F4C, 0x01},
    {0x3F4D, 0x01},
    {0x0408, 0x00},
    {0x0409, 0x00},
    {0x040A, 0x00},
    {0x040B, 0x00},
    {0x040C, 0x12},
    {0x040D, 0x30},
    {0x040E, 0x0D},
    {0x040F, 0xA8},
    {0x034C, 0x12},
    {0x034D, 0x30},
    {0x034E, 0x0D},
    {0x034F, 0xA8},
    {0x0301, 0x06},
    {0x0303, 0x04},
    {0x0305, 0x04},
    {0x0306, 0x00},
    {0x0307, 0xAB},
    {0x030B, 0x01},
    {0x030D, 0x0F},
    {0x030E, 0x01},
    {0x030F, 0x00},
    {0x0310, 0x01},
    {0x3E20, 0x01},
    {0x3E37, 0x01},
    {0x3E3B, 0x00},
    {0x3F78, 0x02},
    {0x3F79, 0x0A},
    {0x3FFE, 0x00},
    {0x3FFF, 0x18},
    {0x5F0A, 0xB2},
    {0xA828, 0x00},
    {0xA829, 0x00},
    {0xA84F, 0x00},
    {0xA850, 0x00},
    {0xB2DF, 0x00},
    {0xB2E5, 0x00},
    {0x0202, 0x0D},
    {0x0203, 0xD8},
    {0x0204, 0x00},
    {0x0205, 0x00},
    {0x020E, 0x01},
    {0x020F, 0x00},
    {0x0100, 0x01},
};
/* Supported sensor mode configurations */
static const struct imx481_mode supported_mode = {
    .width = 4656,
    .height = 3496,
    .hblank = 5120 - 4656,
    .vblank = 3562 - 3496,
    .vblank_min = 66,
    .vblank_max = 66,
    .pclk = 137000000,
    .link_freq_idx = 0,
    .code = MEDIA_BUS_FMT_SRGGB10_1X10,
    .reg_list = {
        .num_of_regs = ARRAY_SIZE(mode_2304x1728_regs),
        .regs = mode_2304x1728_regs,
    },
};

/**
 * to_imx481() - imx481 V4L2 sub-device to imx481 device.
 * @subdev: pointer to imx481 V4L2 sub-device
 *
 * Return: pointer to imx481 device
 */
static inline struct imx481 *to_imx481(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct imx481, sd);
}

/**
 * imx481_read_reg() - Read registers.
 * @imx481: pointer to imx481 device
 * @reg: register address
 * @len: length of bytes to read. Max supported bytes is 4
 * @val: pointer to register value to be filled.
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_read_reg(struct imx481 *imx481, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx481->sd);
	struct i2c_msg msgs[2] = {0};
	u8 addr_buf[2] = {0};
	u8 data_buf[4] = {0};
	int ret;

	if (WARN_ON(len > 4))
		return -EINVAL;

	put_unaligned_be16(reg, addr_buf);

	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = get_unaligned_be32(data_buf);

	return 0;
}

/**
 * imx481_write_reg() - Write register
 * @imx481: pointer to imx481 device
 * @reg: register address
 * @len: length of bytes. Max supported bytes is 4
 * @val: register value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_write_reg(struct imx481 *imx481, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx481->sd);
	u8 buf[6] = {0};

	if (WARN_ON(len > 4))
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

/**
 * imx481_write_regs() - Write a list of registers
 * @imx481: pointer to imx481 device
 * @regs: list of registers to be written
 * @len: length of registers array
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_write_regs(struct imx481 *imx481,
			     const struct imx481_reg *regs, u32 len)
{
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx481_write_reg(imx481, regs[i].address, 1, regs[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * imx481_update_controls() - Update control ranges based on streaming mode
 * @imx481: pointer to imx481 device
 * @mode: pointer to imx481_mode sensor mode
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_update_controls(struct imx481 *imx481,
				  const struct imx481_mode *mode)
{
	int ret;

	ret = __v4l2_ctrl_s_ctrl(imx481->link_freq_ctrl, mode->link_freq_idx);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_s_ctrl(imx481->hblank_ctrl, mode->hblank);
	if (ret)
		return ret;

	return __v4l2_ctrl_modify_range(imx481->vblank_ctrl, mode->vblank_min,
					mode->vblank_max, 1, mode->vblank);
}

/**
 * imx481_update_exp_gain() - Set updated exposure and gain
 * @imx481: pointer to imx481 device
 * @exposure: updated exposure value
 * @gain: updated analog gain value
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_update_exp_gain(struct imx481 *imx481, u32 exposure, u32 gain)
{
	u32 lpfr;
	int ret;

	lpfr = imx481->vblank + imx481->cur_mode->height;

	dev_dbg(imx481->dev, "Set exp %u, analog gain %u, lpfr %u\n",
		exposure, gain, lpfr);

	ret = imx481_write_reg(imx481, imx481_REG_HOLD, 1, 1);
	if (ret)
		return ret;

	ret = imx481_write_reg(imx481, imx481_REG_LPFR, 2, lpfr);
	if (ret)
		goto error_release_group_hold;

	ret = imx481_write_reg(imx481, imx481_REG_EXPOSURE_CIT, 2, exposure);
	if (ret)
		goto error_release_group_hold;

	ret = imx481_write_reg(imx481, imx481_REG_AGAIN, 2, gain);

error_release_group_hold:
	imx481_write_reg(imx481, imx481_REG_HOLD, 1, 0);

	return ret;
}

/**
 * imx481_set_ctrl() - Set subdevice control
 * @ctrl: pointer to v4l2_ctrl structure
 *
 * Supported controls:
 * - V4L2_CID_VBLANK
 * - cluster controls:
 *   - V4L2_CID_ANALOGUE_GAIN
 *   - V4L2_CID_EXPOSURE
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx481 *imx481 =
		container_of(ctrl->handler, struct imx481, ctrl_handler);
	u32 analog_gain;
	u32 exposure;
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		imx481->vblank = imx481->vblank_ctrl->val;

		dev_dbg(imx481->dev, "Received vblank %u, new lpfr %u\n",
			imx481->vblank,
			imx481->vblank + imx481->cur_mode->height);

		ret = __v4l2_ctrl_modify_range(imx481->exp_ctrl,
					       imx481_EXPOSURE_MIN,
					       imx481->vblank +
					       imx481->cur_mode->height -
					       imx481_EXPOSURE_OFFSET,
					       1, imx481_EXPOSURE_DEFAULT);
		break;
	case V4L2_CID_EXPOSURE:
		/* Set controls only if sensor is in power on state */
		if (!pm_runtime_get_if_in_use(imx481->dev))
			return 0;

		exposure = ctrl->val;
		analog_gain = imx481->again_ctrl->val;

		dev_dbg(imx481->dev, "Received exp %u, analog gain %u\n",
			exposure, analog_gain);

		ret = imx481_update_exp_gain(imx481, exposure, analog_gain);

		pm_runtime_put(imx481->dev);

		break;
	default:
		dev_err(imx481->dev, "Invalid control %d\n", ctrl->id);
		ret = -EINVAL;
	}

	return ret;
}

/* V4l2 subdevice control ops*/
static const struct v4l2_ctrl_ops imx481_ctrl_ops = {
	.s_ctrl = imx481_set_ctrl,
};

/**
 * imx481_enum_mbus_code() - Enumerate V4L2 sub-device mbus codes
 * @sd: pointer to imx481 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @code: V4L2 sub-device code enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = supported_mode.code;

	return 0;
}

/**
 * imx481_enum_frame_size() - Enumerate V4L2 sub-device frame sizes
 * @sd: pointer to imx481 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fsize: V4L2 sub-device size enumeration need to be filled
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fsize)
{
	if (fsize->index > 0)
		return -EINVAL;

	if (fsize->code != supported_mode.code)
		return -EINVAL;

	fsize->min_width = supported_mode.width;
	fsize->max_width = fsize->min_width;
	fsize->min_height = supported_mode.height;
	fsize->max_height = fsize->min_height;

	return 0;
}

/**
 * imx481_fill_pad_format() - Fill subdevice pad format
 *                            from selected sensor mode
 * @imx481: pointer to imx481 device
 * @mode: pointer to imx481_mode sensor mode
 * @fmt: V4L2 sub-device format need to be filled
 */
static void imx481_fill_pad_format(struct imx481 *imx481,
				   const struct imx481_mode *mode,
				   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = mode->code;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->format.quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;
}

/**
 * imx481_get_pad_format() - Get subdevice pad format
 * @sd: pointer to imx481 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx481 *imx481 = to_imx481(sd);

	mutex_lock(&imx481->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		imx481_fill_pad_format(imx481, imx481->cur_mode, fmt);
	}

	mutex_unlock(&imx481->mutex);

	return 0;
}

/**
 * imx481_set_pad_format() - Set subdevice pad format
 * @sd: pointer to imx481 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 * @fmt: V4L2 sub-device format need to be set
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx481 *imx481 = to_imx481(sd);
	const struct imx481_mode *mode;
	int ret = 0;

	mutex_lock(&imx481->mutex);

	mode = &supported_mode;
	imx481_fill_pad_format(imx481, mode, fmt);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *framefmt;

		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		ret = imx481_update_controls(imx481, mode);
		if (!ret)
			imx481->cur_mode = mode;
	}

	mutex_unlock(&imx481->mutex);

	return ret;
}

/**
 * imx481_init_state() - Initialize sub-device state
 * @sd: pointer to imx481 V4L2 sub-device structure
 * @sd_state: V4L2 sub-device configuration
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct imx481 *imx481 = to_imx481(sd);
	struct v4l2_subdev_format fmt = { 0 };

	fmt.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	imx481_fill_pad_format(imx481, &supported_mode, &fmt);

	return imx481_set_pad_format(sd, sd_state, &fmt);
}

/**
 * imx481_start_streaming() - Start sensor stream
 * @imx481: pointer to imx481 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_start_streaming(struct imx481 *imx481)
{
	const struct imx481_reg_list *reg_list;
	int ret;

	/* Write sensor mode registers */
	reg_list = &imx481->cur_mode->reg_list;
	ret = imx481_write_regs(imx481, reg_list->regs,
				reg_list->num_of_regs);
	if (ret) {
		dev_err(imx481->dev, "fail to write initial registers\n");
		return ret;
	}

	/* Setup handler will write actual exposure and gain */
	ret =  __v4l2_ctrl_handler_setup(imx481->sd.ctrl_handler);
	if (ret) {
		dev_err(imx481->dev, "fail to setup handler\n");
		return ret;
	}

	/* Delay is required before streaming*/
	usleep_range(7400, 8000);

	/* Start streaming */
	ret = imx481_write_reg(imx481, imx481_REG_MODE_SELECT,
			       1, imx481_MODE_STREAMING);
	if (ret) {
		dev_err(imx481->dev, "fail to start streaming\n");
		return ret;
	}

	return 0;
}

/**
 * imx481_stop_streaming() - Stop sensor stream
 * @imx481: pointer to imx481 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_stop_streaming(struct imx481 *imx481)
{
	return imx481_write_reg(imx481, imx481_REG_MODE_SELECT,
				1, imx481_MODE_STANDBY);
}

/**
 * imx481_set_stream() - Enable sensor streaming
 * @sd: pointer to imx481 subdevice
 * @enable: set to enable sensor streaming
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx481 *imx481 = to_imx481(sd);
	int ret;

	mutex_lock(&imx481->mutex);

	if (enable) {
		ret = pm_runtime_resume_and_get(imx481->dev);
		if (ret)
			goto error_unlock;

		ret = imx481_start_streaming(imx481);
		if (ret)
			goto error_power_off;
	} else {
		imx481_stop_streaming(imx481);
		pm_runtime_put(imx481->dev);
	}

	mutex_unlock(&imx481->mutex);

	return 0;

error_power_off:
	pm_runtime_put(imx481->dev);
error_unlock:
	mutex_unlock(&imx481->mutex);

	return ret;
}

/**
 * imx481_detect() - Detect imx481 sensor
 * @imx481: pointer to imx481 device
 *
 * Return: 0 if successful, -EIO if sensor id does not match
 */
static int imx481_detect(struct imx481 *imx481)
{
	int ret;
	u32 val;

	ret = imx481_read_reg(imx481, imx481_REG_ID, 2, &val);
	if (ret)
		return ret;

	if (val != imx481_ID) {
		dev_err(imx481->dev, "chip id mismatch: %x!=%x\n",
			imx481_ID, val);
		return -ENXIO;
	}

	return 0;
}

/**
 * imx481_check_inclk_freq() - Сheck inclk frequency
 * @rate: inclk frequency from clk_get_rate()
 *
 * Return: 0 if successful, -ENODEV if inclk freq does not match
 */
static int imx481_check_inclk_freq(const int rate)
{
	for (int i = 0; i < ARRAY_SIZE(imx481_inclk_list); i++) {
		if (rate == imx481_inclk_list[i])
			return 0;
	}

	return -ENODEV;
}

/**
 * imx481_parse_hw_config() - Parse HW configuration and check if supported
 * @imx481: pointer to imx481 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_parse_hw_config(struct imx481 *imx481)
{
	struct fwnode_handle *fwnode = dev_fwnode(imx481->dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct fwnode_handle *ep;
	unsigned int i;
	int ret;

	if (!fwnode)
		return -ENXIO;

	/* Request optional reset pin */
	imx481->reset_gpio = devm_gpiod_get_optional(imx481->dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(imx481->reset_gpio)) {
		dev_err(imx481->dev, "failed to get reset gpio %ld\n",
			PTR_ERR(imx481->reset_gpio));
		return PTR_ERR(imx481->reset_gpio);
	}

	/* Get sensor input clock */
	imx481->inclk = devm_clk_get(imx481->dev, NULL);
	if (IS_ERR(imx481->inclk)) {
		dev_err(imx481->dev, "could not get inclk\n");
		return PTR_ERR(imx481->inclk);
	}

	ret = imx481_check_inclk_freq(clk_get_rate(imx481->inclk));
	if (ret) {
		dev_err(imx481->dev, "inclk frequency mismatch\n");
		return ret;
	}

	/* Get optional DT defined regulators */
	for (i = 0; i < ARRAY_SIZE(imx481_supply_names); i++)
		imx481->supplies[i].supply = imx481_supply_names[i];

	ret = devm_regulator_bulk_get(imx481->dev,
				      ARRAY_SIZE(imx481_supply_names),
				      imx481->supplies);
	if (ret)
		return ret;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != imx481_NUM_DATA_LANES) {
		dev_err(imx481->dev,
			"number of CSI2 data lanes %d is not supported\n",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(imx481->dev, "no link frequencies defined\n");
		ret = -EINVAL;
		goto done_endpoint_free;
	}

	for (i = 0; i < bus_cfg.nr_of_link_frequencies; i++)
		if (bus_cfg.link_frequencies[i] == imx481_LINK_FREQ)
			goto done_endpoint_free;

	ret = -EINVAL;

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

/* V4l2 subdevice ops */
static const struct v4l2_subdev_video_ops imx481_video_ops = {
	.s_stream = imx481_set_stream,
};

static const struct v4l2_subdev_pad_ops imx481_pad_ops = {
	.enum_mbus_code = imx481_enum_mbus_code,
	.enum_frame_size = imx481_enum_frame_size,
	.get_fmt = imx481_get_pad_format,
	.set_fmt = imx481_set_pad_format,
};

static const struct v4l2_subdev_ops imx481_subdev_ops = {
	.video = &imx481_video_ops,
	.pad = &imx481_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx481_internal_ops = {
	.init_state = imx481_init_state,
};

/**
 * imx481_power_on() - Sensor power on sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx481 *imx481 = to_imx481(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(imx481_supply_names),
				    imx481->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to enable regulators\n");
		return ret;
	}

	gpiod_set_value_cansleep(imx481->reset_gpio, 0);

	ret = clk_prepare_enable(imx481->inclk);
	if (ret) {
		dev_err(imx481->dev, "fail to enable inclk\n");
		goto error_reset;
	}

	/* At least 45000 MCLK cycles */
	usleep_range(10000, 10200);

	return 0;

error_reset:
	gpiod_set_value_cansleep(imx481->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(imx481_supply_names),
			       imx481->supplies);

	return ret;
}

/**
 * imx481_power_off() - Sensor power off sequence
 * @dev: pointer to i2c device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx481 *imx481 = to_imx481(sd);

	clk_disable_unprepare(imx481->inclk);

	gpiod_set_value_cansleep(imx481->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(imx481_supply_names),
			       imx481->supplies);

	return 0;
}

/**
 * imx481_init_controls() - Initialize sensor subdevice controls
 * @imx481: pointer to imx481 device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_init_controls(struct imx481 *imx481)
{
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr = &imx481->ctrl_handler;
	const struct imx481_mode *mode = imx481->cur_mode;
	u32 lpfr;
	int ret;

	/* set properties from fwnode (e.g. rotation, orientation) */
	ret = v4l2_fwnode_device_parse(imx481->dev, &props);
	if (ret)
		return ret;

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	/* Serialize controls with sensor device */
	ctrl_hdlr->lock = &imx481->mutex;

	/* Initialize exposure and gain */
	lpfr = mode->vblank + mode->height;

	imx481->exp_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					     &imx481_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     imx481_EXPOSURE_MIN,
					     lpfr - imx481_EXPOSURE_OFFSET,
					     imx481_EXPOSURE_STEP,
					     imx481_EXPOSURE_DEFAULT);

	imx481->again_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					       &imx481_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN,
					       imx481_AGAIN_MIN,
					       imx481_AGAIN_MAX,
					       imx481_AGAIN_STEP,
					       imx481_AGAIN_DEFAULT);

	v4l2_ctrl_cluster(2, &imx481->exp_ctrl);

	imx481->vblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx481_ctrl_ops,
						V4L2_CID_VBLANK,
						mode->vblank_min,
						mode->vblank_max,
						1, mode->vblank);

	/* Read only controls */
	imx481->pclk_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
					      &imx481_ctrl_ops,
					      V4L2_CID_PIXEL_RATE,
					      mode->pclk, mode->pclk,
					      1, mode->pclk);

	imx481->link_freq_ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr,
							&imx481_ctrl_ops,
							V4L2_CID_LINK_FREQ,
							ARRAY_SIZE(link_freq) -
							1,
							mode->link_freq_idx,
							link_freq);
	if (imx481->link_freq_ctrl)
		imx481->link_freq_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx481->hblank_ctrl = v4l2_ctrl_new_std(ctrl_hdlr,
						&imx481_ctrl_ops,
						V4L2_CID_HBLANK,
						imx481_REG_MIN,
						imx481_REG_MAX,
						1, mode->hblank);
	if (imx481->hblank_ctrl)
		imx481->hblank_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx481_ctrl_ops, &props);

	if (ctrl_hdlr->error) {
		dev_err(imx481->dev, "control init failed: %d\n",
			ctrl_hdlr->error);
		ret = ctrl_hdlr->error;
		v4l2_ctrl_handler_free(ctrl_hdlr);
		return ret;
	}

	imx481->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

/**
 * imx481_probe() - I2C client device binding
 * @client: pointer to i2c client device
 *
 * Return: 0 if successful, error code otherwise.
 */
static int imx481_probe(struct i2c_client *client)
{
	struct imx481 *imx481;
	int ret;

	imx481 = devm_kzalloc(&client->dev, sizeof(*imx481), GFP_KERNEL);
	if (!imx481)
		return -ENOMEM;

	imx481->dev = &client->dev;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx481->sd, client, &imx481_subdev_ops);
	imx481->sd.internal_ops = &imx481_internal_ops;

	ret = imx481_parse_hw_config(imx481);
	if (ret)
		return dev_err_probe(imx481->dev, ret,
				     "HW configuration is not supported\n");

	mutex_init(&imx481->mutex);

	ret = imx481_power_on(imx481->dev);
	if (ret) {
		dev_err(imx481->dev, "failed to power-on the sensor\n");
		goto error_mutex_destroy;
	}

	/* Check module identity */
	ret = imx481_detect(imx481);
	if (ret) {
		dev_err(imx481->dev, "failed to find sensor: %d\n", ret);
		goto error_power_off;
	}

	/* Set default mode to max resolution */
	imx481->cur_mode = &supported_mode;
	imx481->vblank = imx481->cur_mode->vblank;

	ret = imx481_init_controls(imx481);
	if (ret) {
		dev_err(imx481->dev, "failed to init controls: %d\n", ret);
		goto error_power_off;
	}

	/* Initialize subdev */
	imx481->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx481->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx481->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx481->sd.entity, 1, &imx481->pad);
	if (ret) {
		dev_err(imx481->dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx481->sd);
	if (ret < 0) {
		dev_err(imx481->dev,
			"failed to register async subdev: %d\n", ret);
		goto error_media_entity;
	}

	pm_runtime_set_active(imx481->dev);
	pm_runtime_enable(imx481->dev);
	pm_runtime_idle(imx481->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&imx481->sd.entity);
error_handler_free:
	v4l2_ctrl_handler_free(imx481->sd.ctrl_handler);
error_power_off:
	imx481_power_off(imx481->dev);
error_mutex_destroy:
	mutex_destroy(&imx481->mutex);

	return ret;
}

static void imx481_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx481 *imx481 = to_imx481(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx481_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	mutex_destroy(&imx481->mutex);
}

static const struct dev_pm_ops imx481_pm_ops = {
	SET_RUNTIME_PM_OPS(imx481_power_off, imx481_power_on, NULL)
};

static const struct of_device_id imx481_of_match[] = {
	{ .compatible = "sony,imx481" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx481_of_match);

static struct i2c_driver imx481_driver = {
	.probe = imx481_probe,
	.remove = imx481_remove,
	.driver = {
		.name = "imx481",
		.pm = &imx481_pm_ops,
		.of_match_table = imx481_of_match,
	},
};

module_i2c_driver(imx481_driver);

MODULE_DESCRIPTION("Sony imx481 sensor driver");
MODULE_LICENSE("GPL");
