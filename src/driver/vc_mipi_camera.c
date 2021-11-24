#include <linux/module.h>
#include <linux/of.h>
#include <media/tegra-v4l2-camera.h>
#include <media/tegracam_core.h>
#include "vc_mipi_core.h"
#include "vc_mipi_modules.h"

// #define VC_CTRL_VALUE


static struct vc_cam *tegracam_to_cam(struct tegracam_device *tc_dev)
{
	return (struct vc_cam *)tegracam_get_privdata(tc_dev);
}

static struct sensor_mode_properties *tegracam_to_mode0(struct tegracam_device *tc_dev) 
{
	struct sensor_properties *sensor = &tc_dev->s_data->sensor_props;

	if (sensor->sensor_modes != NULL && sensor->num_modes > 0) {
		return &sensor->sensor_modes[0];
	}
	return NULL;
}

void vc_adjust_cam_ctrls(struct vc_cam *cam, __u32 *width, __u32 *height)
{
	__u8 num_lanes = vc_core_get_num_lanes(cam);
	__u32 code = vc_core_get_format(cam);
	int trigger_mode = vc_mod_get_trigger_mode(cam);
	int trigger_enabled = 0;

	// Triggermodi
	// 0: FLAG_TRIGGER_DISABLE
	// 1: FLAG_TRIGGER_EXTERNAL
	// 2: FLAG_TRIGGER_PULSEWIDTH
	// 3: FLAG_TRIGGER_SELF
	// 4: FLAG_TRIGGER_SINGLE
	// 5: FLAG_TRIGGER_SYNC
	// 6: FLAG_TRIGGER_STREAM_EDGE
	// 7: FLAG_TRIGGER_STREAM_LEVEL

	switch (trigger_mode) {
		case 0: case 5: case 6: case 7: default:
			trigger_enabled = 0;
			break;
		case 1: case 2: case 3: case 4:
			trigger_enabled = 1;
			break;
	}

	// This error is dependent on VMAX, SHS and frame.height
	// TEGRA_VI_CSI_ERROR_STATUS=0x00000001 (Bits 3-0: h>,<, w>,<) => width is to high
	// TEGRA_VI_CSI_ERROR_STATUS=0x00000002 (Bits 3-0: h>,<, w>,<) => width is to low
	// TEGRA_VI_CSI_ERROR_STATUS=0x00000004 (Bits 3-0: h>,<, w>,<) => height is to high
	// TEGRA_VI_CSI_ERROR_STATUS=0x00000008 (Bits 3-0: h>,<, w>,<) => height is to low
	switch (cam->desc.mod_id) {
	case MOD_ID_IMX178: // Active pixels 3072 x 2076
		*height = 2047;
		if (trigger_enabled) {
			*height = 2048;
		}
		break;

	case MOD_ID_IMX183: // Active pixels 5440 x 3648
		if (trigger_enabled) {
			switch (code) {
			case MEDIA_BUS_FMT_Y8_1X8: 	case MEDIA_BUS_FMT_SRGGB8_1X8:
			case MEDIA_BUS_FMT_Y10_1X10: 	case MEDIA_BUS_FMT_SRGGB10_1X10:
				*height = 3636; 
				break;
			}
		}
		break;

	case MOD_ID_IMX226: // Active pixels 3840 x 3046 (FPGA)
		switch (cam->desc.mod_rev) {
		case 0x0b:
			// Rev: 0x0b 
			// 2 Lanes, Freerun und Trigger 1, 6 und 7
			//          RGGB, RG10, RG12 => 3045 
			// 4 Lanes, Freerun und Trigger 1, 6 und 7
			//          RGGB, RG10 => 3044
			//          RG12 => Multi-bit transmission error
			// Fazit:
			//          Im Trigger Mode 1 ist die Frequenz instabil.
			//          Im Trigger Mode 6, 7 hat der flash out immer 100 ns + die Dauer der Belichtungzeit.
			// Rev: 0x08
			// Fazit:
			//          Im Triggermodus ist die Farbdarstellung nicht mehr richtig. (Magenta)
			*height = 3045;
			if (num_lanes == 4) {
				(*height)--;
			}
			break;
		default:
		case 0x08:
			if (!trigger_enabled) {
				switch (code) {
				case MEDIA_BUS_FMT_Y8_1X8: 	case MEDIA_BUS_FMT_SRGGB8_1X8:
				case MEDIA_BUS_FMT_Y10_1X10: 	case MEDIA_BUS_FMT_SRGGB10_1X10:
					*height = 3044;
					break;
				}
			} else {
				switch (code) {
				case MEDIA_BUS_FMT_Y8_1X8: 	case MEDIA_BUS_FMT_SRGGB8_1X8:
				case MEDIA_BUS_FMT_Y10_1X10: 	case MEDIA_BUS_FMT_SRGGB10_1X10:
					*height = 3045;
					break;
				case MEDIA_BUS_FMT_Y12_1X12: 	case MEDIA_BUS_FMT_SRGGB12_1X12:
					// 3045 sometimes height to short
					// 3046 sometimes height to long
					*height = 3046; 
					break;
				}
			}
			if (num_lanes == 4) {
				(*height)--;
			}
			break;
		}
		break;

	case MOD_ID_IMX252: // Active pixels 2048 x 1536
		if (num_lanes == 4) {
			(*height)--;
		}
		break;

	case MOD_ID_IMX273: // Active pixels 1440 x 1080
		if (num_lanes == 4) {
			switch (code) {
			case MEDIA_BUS_FMT_Y8_1X8: 	case MEDIA_BUS_FMT_SRGGB8_1X8:
				*width = 1472;
				break;
			}
			(*height)--;
		}
		break;

	case MOD_ID_IMX296: // Active pixels 1440 x 1080
		if (trigger_enabled) {
			*height = 1081;
		}
		break;

	case MOD_ID_IMX412: // Active pixels 4056 x 3040
		*width = 4032;
		if (num_lanes == 4) {
			(*height)--;
		}
		break;

	case MOD_ID_IMX327: // Active pixels 1920 x 1080
	case MOD_ID_OV9281: // Active pixels 1280 x 800
		// Test Ok
		break;
	}
}

__u32 g_width = 0;
__u32 g_height = 0;
__u32 g_line_length = 0;

void vc_adjust_tegracam(struct tegracam_device *tc_dev)
{
	struct vc_cam *cam = tegracam_to_cam(tc_dev);
	struct device *dev = tc_dev->s_data->dev;
	struct camera_common_frmfmt *frmfmt1 = (struct camera_common_frmfmt *)tc_dev->sensor_ops->frmfmt_table;
	struct camera_common_frmfmt *frmfmt2 = (struct camera_common_frmfmt *)tc_dev->s_data->frmfmt;
	struct sensor_mode_properties *mode = tegracam_to_mode0(tc_dev);
	__u32 width = cam->ctrl.frame.width;
	__u32 height = cam->ctrl.frame.height;
	__u32 line_length = cam->ctrl.frame.width;

	vc_adjust_cam_ctrls(cam, &width, &height);

	if (g_width != 0) {
		width = g_width;
	}
	if (g_height != 0) {
		height = g_height;
	}
	if (g_line_length != 0) {
		line_length = g_line_length;
	}

	if (width != cam->ctrl.frame.width || height != cam->ctrl.frame.height)
		vc_notice(dev, "%s(): Adjust tegracam framework settings (w: %u h: %u l: %u)\n", __FUNCTION__, 
			width, height, line_length);

	switch (cam->desc.mod_id) {
	case MOD_ID_IMX273:
	case MOD_ID_IMX412: 
		cam->state.frame.width = width;
	}

	// TODO: Problem! When the format is changed set_mode is called to late in s_stream 
	//       to make the change active. Currently it is necessary to start streaming twice!
	tc_dev->s_data->def_width = width;
	tc_dev->s_data->def_height = height;
	tc_dev->s_data->fmt_width = width;
	tc_dev->s_data->fmt_height = height;
	frmfmt1[0].size.width = width;
	frmfmt1[0].size.height = height;
	frmfmt2[0].size.width = width;
	frmfmt2[0].size.height = height;
	mode->image_properties.width = width;
	mode->image_properties.height = height;
	mode->image_properties.line_length = line_length;
}

static int vc_set_mode(struct tegracam_device *tc_dev)
{
	struct vc_cam *cam = tegracam_to_cam(tc_dev);
	vc_adjust_tegracam(tc_dev);
	return vc_core_set_format(cam , tc_dev->s_data->colorfmt->code);
}

static int vc_read_reg(struct camera_common_data *s_data, __u16 addr, __u8 *val)
{
    	int ret = 0;
    	__u32 reg_val = 0;

    	ret = regmap_read(s_data->regmap, addr, &reg_val); 
    	if (ret) {
       		vc_err(s_data->dev, "%s(): i2c read failed! (addr: 0x%04x)\n", __FUNCTION__, addr);
       		return ret;
    	}
    	
	*val = reg_val & 0xff;
	vc_notice(s_data->dev, "%s(): i2c read (addr: 0x%04x => value: 0x%02x)\n", __FUNCTION__, addr, *val);

    	return 0;
}

static int vc_write_reg(struct camera_common_data *s_data, __u16 addr, __u8 val)
{
    	int ret = 0;

    	ret = regmap_write(s_data->regmap, addr, val);
    	if (ret) {
        	vc_err(s_data->dev, "%s(): i2c write failed! (addr: 0x%04x <= value: 0x%02x)\n", __FUNCTION__, addr, val);
    	}

	vc_notice(s_data->dev, "%s(): i2c write (addr: 0x%04x => value: 0x%02x)\n", __FUNCTION__, addr, val);

    	return ret;
}

static int vc_set_gain(struct tegracam_device *tc_dev, __s64 val)
{
	struct vc_cam *cam = tegracam_to_cam(tc_dev);
	struct sensor_mode_properties *mode = tegracam_to_mode0(tc_dev);
	struct sensor_control_properties *control;
	int gain = 0;

	if (mode != NULL) {
		control = &mode->control_properties;

		gain =  ((cam->ctrl.gain.max - cam->ctrl.gain.min)*val)/(control->max_gain_val - control->min_gain_val) 
			+ cam->ctrl.gain.min;

		return vc_sen_set_gain(cam, gain);
	}

	return -EINVAL;
}

static int vc_set_exposure(struct tegracam_device *tc_dev, __s64 val)
{
	struct vc_cam *cam = tegracam_to_cam(tc_dev);
	return vc_sen_set_exposure(cam, val);
}

static int vc_set_frame_rate(struct tegracam_device *tc_dev, __s64 val)
{
	struct vc_cam *cam = tegracam_to_cam(tc_dev);
	int ret = 0;

	ret  = vc_core_set_framerate(cam, val);
	ret |= vc_sen_set_exposure(cam, cam->state.exposure);

	return ret;
}

static int vc_set_trigger_mode(struct tegracam_device *tc_dev, __s64 val)
{
	struct vc_cam *cam = tegracam_to_cam(tc_dev);
	vc_adjust_tegracam(tc_dev);
	return vc_mod_set_trigger_mode(cam, val);
}

static int vc_set_flash_mode(struct tegracam_device *tc_dev, __s64 val)
{
	struct vc_cam *cam = tegracam_to_cam(tc_dev);
	return vc_mod_set_io_mode(cam, val);
}

__u32 g_sleep = 50;

#ifdef VC_CTRL_VALUE
static int vc_set_value(struct tegracam_device *tc_dev, __s64 val)
{
	struct vc_cam *cam = tegracam_to_cam(tc_dev);
	struct device *dev = vc_core_get_sen_device(cam);
	
	if (0 < val && val < 10000) {
		g_width = val;
		vc_notice(dev, "%s(): Set testing width = %u", __FUNCTION__, g_width);
	}
	if (10000 <= val && val < 20000) {
		g_height = val - 10000;
		vc_notice(dev, "%s(): Set testing height = %u", __FUNCTION__, g_height);
	}
	if (20000 <= val && val < 30000) {
		g_line_length = val - 20000;
		vc_notice(dev, "%s(): Set testing line_length = %u", __FUNCTION__, g_line_length);
	}
	if (30000 <= val && val < 40000) {
		g_sleep = val - 30000;
		vc_notice(dev, "%s(): Set testing sleep = %u", __FUNCTION__, g_sleep);
	}
	if (40000 <= val && val < 50000) {
		__u32 gain = val - 40000;
		vc_sen_set_gain(cam, gain);
		vc_notice(dev, "%s(): Set testing gain = %u", __FUNCTION__, gain);
	}
	if (50000 <= val && val < 60000) {
		__u32 width = val - 50000;
		vc_sen_set_roi(cam, 0, 0, width, cam->state.frame.height);
		vc_notice(dev, "%s(): Set testing roi width = %u", __FUNCTION__, width);
	}
	if (60000 <= val && val < 70000) {
		__u32 height = val - 60000;
		vc_sen_set_roi(cam, 0, 0, cam->state.frame.width, height);
		vc_notice(dev, "%s(): Set testing roi height = %u", __FUNCTION__, height);
	}
	if (70000 <= val && val < 80000) {
		__u32 vmax = val - 70000;
		cam->ctrl.expo_vmax = vmax;
		vc_notice(dev, "%s(): Set testing vmax = %u", __FUNCTION__, vmax);
	}

	return 0;
}
#endif

static int vc_start_streaming(struct tegracam_device *tc_dev)
{
	struct vc_cam *cam = tegracam_to_cam(tc_dev);
	int reset;
	int ret = 0;

	ret  = vc_mod_set_mode(cam, &reset);
	ret |= vc_sen_set_roi(cam, 0, 0, cam->state.frame.width, cam->state.frame.height);
	if (!ret && reset) {
	ret |= vc_sen_set_gain(cam, cam->state.gain);
	ret |= vc_sen_set_exposure(cam, cam->state.exposure);
	}
	
	ret |= vc_sen_start_stream(cam);
	usleep_range(1000*g_sleep, 1000*g_sleep);

	return ret;
}

static int vc_stop_streaming(struct tegracam_device *tc_dev)
{
	struct vc_cam *cam = tegracam_to_cam(tc_dev);
	int ret = 0;
	
	ret = vc_sen_stop_stream(cam);

	return ret;
}

// NOTE: Don't remove this function. It is needed by the Tegra Framework. 
static int vc_set_group_hold(struct tegracam_device *tc_dev, bool val) {return 0;}

// NOTE: Don't remove this function. It is needed by the Tegra Framework. 
static int vc_power_get(struct tegracam_device *tc_dev) {return 0;}

// NOTE: Don't remove this function. It is needed by the Tegra Framework. 
// (called by tegracam_device_register)
static struct camera_common_pdata *vc_parse_dt(struct tegracam_device *tc_dev)
{
	return devm_kzalloc(tc_dev->dev, sizeof(struct camera_common_pdata), GFP_KERNEL);
}

static struct camera_common_sensor_ops vc_sensor_ops = {
    	.frmfmt_table = NULL,
	.read_reg = vc_read_reg,
	.write_reg = vc_write_reg,
	.set_mode = vc_set_mode,
	.start_streaming = vc_start_streaming,
	.stop_streaming = vc_stop_streaming,
	.power_get = vc_power_get,
	.parse_dt = vc_parse_dt,
};

int vc_init_frmfmt(struct device *dev, struct vc_cam *cam)
{
	struct camera_common_frmfmt *frmfmt = NULL;
	int *fps = NULL;

	vc_dbg(dev, "%s(): Allocate memory and init frame parameters\n", __FUNCTION__);

	if (vc_sensor_ops.frmfmt_table == NULL) {
		frmfmt = devm_kzalloc(dev, sizeof(*frmfmt), GFP_KERNEL);
		if (!frmfmt)
			return -ENOMEM;
	
		vc_sensor_ops.frmfmt_table = frmfmt;
		vc_sensor_ops.numfrmfmts = 1;

		fps = devm_kzalloc(dev, sizeof(int), GFP_KERNEL);
		if (!fps)
			return -ENOMEM;

		frmfmt->framerates = fps;
		frmfmt->num_framerates = 1;
	}

	if (frmfmt != NULL && fps != NULL) {
		fps[0] = cam->ctrl.framerate.def;
		
		frmfmt->size.width = cam->ctrl.frame.width;
		frmfmt->size.height = cam->ctrl.frame.height;
		frmfmt->hdr_en = 0;
		frmfmt->mode = 0;

		vc_notice(dev, "%s(): Init frame (width: %d, height: %d, fps: %d)\n", __FUNCTION__,
			frmfmt->size.width, frmfmt->size.height, frmfmt->framerates[0]);
		return 0;
	}

	return -ENOMEM;
}

void vc_init_image(struct tegracam_device *tc_dev)
{
	struct vc_cam *cam = tegracam_to_cam(tc_dev);
	struct device *dev = tc_dev->dev;
	struct sensor_mode_properties *mode = tegracam_to_mode0(tc_dev);
	struct sensor_image_properties *image;
	__u32 code;

	if (mode != NULL) {
		image = &mode->image_properties;
		image->width = cam->ctrl.frame.width;
		image->height = cam->ctrl.frame.height;
		image->line_length = cam->ctrl.frame.width;

		code = vc_core_get_format(cam);
		switch(code) {
		case MEDIA_BUS_FMT_Y8_1X8:       image->pixel_format = V4L2_PIX_FMT_GREY;    break;
		case MEDIA_BUS_FMT_Y10_1X10:     image->pixel_format = V4L2_PIX_FMT_Y10;     break;
		case MEDIA_BUS_FMT_Y12_1X12:     image->pixel_format = V4L2_PIX_FMT_Y12;     break;
		case MEDIA_BUS_FMT_SRGGB8_1X8:   image->pixel_format = V4L2_PIX_FMT_SRGGB8;  break;
		case MEDIA_BUS_FMT_SRGGB10_1X10: image->pixel_format = V4L2_PIX_FMT_SRGGB10; break;
		case MEDIA_BUS_FMT_SRGGB12_1X12: image->pixel_format = V4L2_PIX_FMT_SRGGB12; break;
		case MEDIA_BUS_FMT_SGBRG8_1X8:   image->pixel_format = V4L2_PIX_FMT_SGBRG8;  break;
		case MEDIA_BUS_FMT_SGBRG10_1X10: image->pixel_format = V4L2_PIX_FMT_SGBRG10; break;
		case MEDIA_BUS_FMT_SGBRG12_1X12: image->pixel_format = V4L2_PIX_FMT_SGBRG12; break;
		}

		tc_dev->s_data->colorfmt = camera_common_find_datafmt(code);
		
		vc_notice(dev, "%s(): Init image (width: %d, height: %d, line_length: %d, pixel_t: %c%c%c%c)\n", __FUNCTION__,
			image->width, image->height, image->line_length, 
			(char)((image->pixel_format >>  0)&0xFF),
			(char)((image->pixel_format >>  8)&0xFF),
			(char)((image->pixel_format >> 16)&0xFF),
			(char)((image->pixel_format >> 24)&0xFF));
	}
}

static int vc_init_lanes(struct tegracam_device *tc_dev) 
{
	struct vc_cam *cam = tegracam_to_cam(tc_dev);
	struct device *dev = tc_dev->dev;
	struct sensor_mode_properties *mode = tegracam_to_mode0(tc_dev);
	struct sensor_signal_properties *signal;
	int ret;

	if (mode != NULL) {
		signal = &mode->signal_properties;

		ret = vc_core_set_num_lanes(cam, signal->num_lanes);
		if (ret) 
			return ret;

		vc_notice(dev, "%s(): Init lanes (num_lanes: %d)\n", __FUNCTION__, signal->num_lanes);
		return 0;
	}
	return -EINVAL;
}

static int read_property_u32(struct device_node *node, const char *name, int radix, __u32 *value)
{
	const char *str;
	int ret = 0;

    	ret = of_property_read_string(node, name, &str);
    	if (ret)
        	return -ENODATA;

	ret = kstrtou32(str, radix, value);
	if (ret)
		return -EFAULT;

    	return 0;
}

static void vc_init_io(struct device *dev, struct vc_cam *cam)
{
	struct device_node *node = dev->of_node;
	int value = 1;
	int ret = 0;

	vc_notice(dev, "%s(): Init trigger and flash mode\n", __FUNCTION__);

	if (node != NULL) {
		ret = read_property_u32(node, "trigger_mode", 10, &value);
		if (ret) {
			vc_err(dev, "%s(): Unable to read trigger_mode from device tree!\n", __FUNCTION__);
		} else {
			vc_mod_set_trigger_mode(cam, value);
		}

		ret = read_property_u32(node, "flash_mode", 10, &value);
		if (ret) {
			vc_err(dev, "%s(): Unable to read flash_mode from device tree!\n", __FUNCTION__);
		} else {
			vc_mod_set_io_mode(cam, value);
		}
	}
}

void vc_init_controls(struct tegracam_device *tc_dev) 
{
	struct vc_cam *cam = tegracam_to_cam(tc_dev);
	struct device *dev = tc_dev->dev;
	struct sensor_mode_properties *mode = tegracam_to_mode0(tc_dev);
	struct sensor_control_properties *control;

	if (mode != NULL) {
		control = &mode->control_properties;

		vc_notice(dev, "%s(): Read control gain (min: %d, max: %d, default: %d)\n", __FUNCTION__, 
			control->min_gain_val, control->max_gain_val, control->default_gain);

		vc_notice(dev, "%s(): Overwrite control exposure (min: %d, max: %d, default: %d)\n", __FUNCTION__, 
			cam->ctrl.exposure.min, cam->ctrl.exposure.max, cam->ctrl.exposure.def);
		control->exposure_factor = 1;
		control->min_exp_time.val = cam->ctrl.exposure.min;
		control->max_exp_time.val = cam->ctrl.exposure.max;
		control->default_exp_time.val = cam->ctrl.exposure.def;
		control->step_exp_time.val = 1;

		vc_notice(dev, "%s(): Overwrite control framerate (min: %d, max: %d, default: %d)\n", __FUNCTION__, 
			cam->ctrl.framerate.min, cam->ctrl.framerate.max, cam->ctrl.framerate.def);
		control->framerate_factor = 1;
		control->min_framerate = cam->ctrl.framerate.min;
		control->max_framerate = cam->ctrl.framerate.max;
		control->default_framerate = cam->ctrl.framerate.def;
		control->step_framerate = 1;
	}

	// NOTE: Set this state to enable controls in tegracam_ctrls.c -> tegracam_set_ctrls
	tc_dev->s_data->power->state = SWITCH_ON;
}

static const struct regmap_config vc_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.use_single_rw = true,
};

static const __u32 ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_TRIGGER_MODE,
	TEGRA_CAMERA_CID_FLASH_MODE,
#ifdef VC_CTRL_VALUE
	TEGRA_CAMERA_CID_VALUE,
#endif
};

static struct tegracam_ctrl_ops vc_ctrl_ops = {
	.numctrls = ARRAY_SIZE(ctrl_cid_list),
	.ctrl_cid_list = ctrl_cid_list,
	.set_gain = vc_set_gain,
	.set_exposure = vc_set_exposure,
	.set_frame_rate = vc_set_frame_rate,
	.set_trigger_mode = vc_set_trigger_mode,
	.set_flash_mode = vc_set_flash_mode,
	.set_group_hold = vc_set_group_hold,
#ifdef VC_CTRL_VALUE
	.set_value = vc_set_value,
#endif
};

static int vc_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	// --------------------------------------------------------------------
	// NOTE: Don't modify this lines of code. It will cause a kernel crash.
	// "Unable to handle kernel paging request at virtual address 7dabb7951ec6f65c"
	struct device *dev = &client->dev;
	struct vc_cam *cam;
	struct tegracam_device *tc_dev;
	int ret;

	vc_notice(dev, "%s(): Probing UNIVERSAL VC MIPI Driver\n", __func__);
	// --------------------------------------------------------------------

 	cam = devm_kzalloc(dev, sizeof(struct vc_cam), GFP_KERNEL);
	if (!cam)
		return -ENOMEM;

	tc_dev = devm_kzalloc(dev, sizeof(struct tegracam_device), GFP_KERNEL);
	if (!tc_dev)
		return 0;

	ret = vc_core_init(cam, client);
	if (ret) {
		vc_err(dev, "%s(): Error in vc_core_init!\n", __func__);
		return 0;
	}
	vc_init_io(dev, cam);
	vc_init_frmfmt(dev, cam);

	// Defined in tegracam_core.c
	// Initializes 
	//   * tc_dev->s_data
	// Calls 
	//   * camera_common_initialize
	tc_dev->client = client;
	tc_dev->dev = dev;
	strncpy(tc_dev->name, "vc_mipi", sizeof(tc_dev->name));
	tc_dev->dev_regmap_config = &vc_regmap_config;
	tc_dev->sensor_ops = &vc_sensor_ops;
	ret = tegracam_device_register(tc_dev);
	if (ret) {
		vc_err(dev, "%s(): Tegra camera device registration failed\n", __FUNCTION__);
		// goto free_vc_core;
		return 0;
	}

	// Defined in tegracam_core.c
	// Initializes
	//   * tc_dev->priv
	//   * tc_dev->s_data->priv
	tegracam_set_privdata(tc_dev, (void *)cam);
	
	// Defined in tegracam_v4l2.c
	// Initializes
	//   * tc_dev->s_data->tegracam_ctrl_hdl
	tc_dev->tcctrl_ops = &vc_ctrl_ops;
	ret = tegracam_v4l2subdev_register(tc_dev, true);
	if (ret) {
       		vc_err(dev, "%s(): Tegra camera subdev registration failed\n", __FUNCTION__);
       		// goto unregister_tc_dev;
		return 0;
    	}

	// This functions need tc_dev->s_data to be initialized.
	vc_init_image(tc_dev);
	vc_init_controls(tc_dev);
	ret = vc_init_lanes(tc_dev);
	if (ret)
		goto unregister_subdev;

	vc_adjust_tegracam(tc_dev);
	
	return 0;

unregister_subdev:
	tegracam_v4l2subdev_unregister(tc_dev);
	return 0;
}

static int vc_remove(struct i2c_client *client)
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct tegracam_device *tc_dev = s_data->tegracam_ctrl_hdl->tc_dev;

	tegracam_v4l2subdev_unregister(tc_dev);
	tegracam_device_unregister(tc_dev);
	return 0;
}

static const struct i2c_device_id vc_id[] = {
	{ "vc_mipi", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, vc_id);

static const struct of_device_id vc_dt_ids[] = {
	{ .compatible = "nvidia,vc_mipi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, vc_dt_ids);

static struct i2c_driver vc_i2c_driver = {
	.driver = {
		.name = "vc_mipi",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(vc_dt_ids),
	},
	.id_table = vc_id,
	.probe = vc_probe,
	.remove = vc_remove,
};
module_i2c_driver(vc_i2c_driver);

MODULE_VERSION("0.5.0");
MODULE_DESCRIPTION("Media Controller driver for VC MIPI Cameras");
MODULE_AUTHOR("Vision Components GmbH <mipi-tech@vision-components.com>");
MODULE_LICENSE("GPL v2");