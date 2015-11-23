
/*
 * es8316.c -- es8316 ALSA SoC audio driver
 * Copyright Everest Semiconductor Co.,Ltd
 *
 * Author: David Yang <yangxiaohua@everest-semi.com>
 *
 * Based on es8316.c
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <linux/proc_fs.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/regmap.h>
#include <linux/kernel.h>
#include "es8316.h"

#if 1
#define DBG(x...) printk(x)
#else
#define DBG(x...) do { } while (0)
#endif
#define alsa_dbg DBG

#define dmic_used  1
#define amic_used  0

#define INVALID_GPIO -1
int es8316_spk_con_gpio = INVALID_GPIO;
int es8316_hp_con_gpio = INVALID_GPIO;
int es8316_hp_det_gpio = INVALID_GPIO;
static int HP_IRQ;
static int hp_irq_flag;
static int es8316_init_reg;

#define GPIO_LOW  0
#define GPIO_HIGH 1
#ifndef es8316_DEF_VOL
#define es8316_DEF_VOL		0x1e
#endif

struct snd_soc_codec *es8316_codec;
static int es8316_init_regs(struct snd_soc_codec *codec);
static int es8316_set_bias_level(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level);

static const struct reg_default es8316_reg_defaults[] = {
	{0x00, 0x03}, {0x01, 0x03}, {0x02, 0x00}, {0x03, 0x20},
	{0x04, 0x11}, {0x05, 0x00}, {0x06, 0x11}, {0x07, 0x00},
	{0x08, 0x00}, {0x09, 0x01}, {0x0a, 0x00}, {0x0b, 0x00},
	{0x0c, 0xf8}, {0x0d, 0x3f}, {0x0e, 0x00}, {0x0f, 0x00},
	{0x10, 0x01}, {0x11, 0xfc}, {0x12, 0x28}, {0x13, 0x00},
	{0x14, 0x00}, {0x15, 0x33}, {0x16, 0x00}, {0x17, 0x00},
	{0x18, 0x88}, {0x19, 0x06}, {0x1a, 0x22}, {0x1b, 0x03},
	{0x1c, 0x0f}, {0x1d, 0x00}, {0x1e, 0x80}, {0x1f, 0x80},
	{0x20, 0x00}, {0x21, 0x00}, {0x22, 0xc0}, {0x23, 0x00},
	{0x24, 0x01}, {0x25, 0x08}, {0x26, 0x10}, {0x27, 0xc0},
	{0x28, 0x00}, {0x29, 0x1c}, {0x2a, 0x00}, {0x2b, 0xb0},
	{0x2c, 0x32}, {0x2d, 0x03}, {0x2e, 0x00}, {0x2f, 0x11},
	{0x30, 0x10}, {0x31, 0x00}, {0x32, 0x00}, {0x33, 0xc0},
	{0x34, 0xc0}, {0x35, 0x1f}, {0x36, 0xf7}, {0x37, 0xfd},
	{0x38, 0xff}, {0x39, 0x1f}, {0x3a, 0xf7}, {0x3b, 0xfd},
	{0x3c, 0xff}, {0x3d, 0x1f}, {0x3e, 0xf7}, {0x3f, 0xfd},
	{0x40, 0xff}, {0x41, 0x1f}, {0x42, 0xf7}, {0x43, 0xfd},
	{0x44, 0xff}, {0x45, 0x1f}, {0x46, 0xf7}, {0x47, 0xfd},
	{0x48, 0xff}, {0x49, 0x1f}, {0x4a, 0xf7}, {0x4b, 0xfd},
	{0x4c, 0xff}, {0x4d, 0x00}, {0x4e, 0x00}, {0x4f, 0xff},
	{0x50, 0x00}, {0x51, 0x00}, {0x52, 0x00}, {0x53, 0x00},
};

static bool es8316_writeable(struct device *dev, unsigned int reg)
{
	if (reg <= 90)
		return true;
	else
		return false;
}
static bool es8316_readable(struct device *dev, unsigned int reg)
{
	if (reg <= 90)
		return true;
	else
		return false;
}
static bool es8316_volatile(struct device *dev, unsigned int reg)
{
	if (reg <= 90)
		return true;
	else
		return false;
}
/* codec private data */
struct es8316_priv {
	struct regmap *regmap;
	unsigned int dmic_amic;
	unsigned int sysclk;
	struct snd_pcm_hw_constraint_list *sysclk_constraints;
};

/*
 * es8316_reset
 * write value 0xff to reg0x00, the chip will be in reset mode
 * then, writer 0x00 to reg0x00, unreset the chip
 */
static int es8316_reset(struct snd_soc_codec *codec)
{
	snd_soc_write(codec, ES8316_RESET_REG00, 0x7b);
	msleep(100);

	return snd_soc_write(codec, ES8316_RESET_REG00, 0x40);
}
/*
 * es8316_hp_state_query
 * Check the register 0x4F, return the register value
 * User should update this code according the headphone jack configuratio
 */
static int es8316_hp_state_query(struct snd_soc_codec *codec)
{
	unsigned int retv, tmp;
	struct es8316_priv *es8316 = snd_soc_codec_get_drvdata(codec);

	retv = snd_soc_read(codec, ES8316_GPIO_FLAG);
	tmp  = retv & 0x06;

	return tmp;
}
/*
 * es8316S Controls
 */
/* #define DECLARE_TLV_DB_SCALE(name, min, step, mute)
 * static const DECLARE_TLV_DB_SCALE(hpout_vol_tlv, -4800, 1200, 0); */
static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -9600, 50, 1);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -9600, 50, 1);
static const DECLARE_TLV_DB_SCALE(hpmixer_gain_tlv, -1200, 150, 0);
static const DECLARE_TLV_DB_SCALE(mic_bst_tlv, 0, 1200, 0);
/* static const DECLARE_TLV_DB_SCALE(linin_pga_tlv, 0, 300, 0);
 * {0, +3, +6, +9, +12, +15, +18, +21, +24,+27,+30,+33} dB */
static unsigned int linin_pga_tlv[] = {
	TLV_DB_RANGE_HEAD(12),
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(300, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(600, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(900, 0, 0),
	4, 4, TLV_DB_SCALE_ITEM(1200, 0, 0),
	5, 5, TLV_DB_SCALE_ITEM(1500, 0, 0),
	6, 6, TLV_DB_SCALE_ITEM(1800, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(2100, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(2400, 0, 0),
};
static unsigned int hpout_vol_tlv[] = {
	TLV_DB_RANGE_HEAD(1),
	0, 3, TLV_DB_SCALE_ITEM(-4800, 1200, 0),

};

static const char * const alc_func_txt[] = {"Off", "On"};
static const struct soc_enum alc_func =
SOC_ENUM_SINGLE(ES8316_ADC_ALC1_REG29, 6, 2, alc_func_txt);

static const char * const ng_type_txt[] = {"Constant PGA Gain",
	"Mute ADC Output"};
static const struct soc_enum ng_type =
SOC_ENUM_SINGLE(ES8316_ADC_ALC6_REG2E, 6, 2, ng_type_txt);

static const char * const adcpol_txt[] = {"Normal", "Invert"};
static const struct soc_enum adcpol =
SOC_ENUM_SINGLE(ES8316_ADC_MUTE_REG26, 1, 2, adcpol_txt);
static const char * const dacpol_txt[] = {"Normal", "R Invert", "L Invert",
	"L + R Invert"};
static const struct soc_enum dacpol =
SOC_ENUM_SINGLE(ES8316_DAC_SET1_REG30, 0, 4, dacpol_txt);

static const struct snd_kcontrol_new es8316_snd_controls[] = {
	/* HP OUT VOLUME */
	SOC_DOUBLE_TLV("HP Playback Volume", ES8316_CPHP_ICAL_VOL_REG18,
			4, 0, 0, 1, hpout_vol_tlv),
	/* HPMIXER VOLUME Control */
	SOC_DOUBLE_TLV("HPMixer Gain", ES8316_HPMIX_VOL_REG16,
			0, 4, 7, 0, hpmixer_gain_tlv),

	/* DAC Digital controls */
	SOC_DOUBLE_R_TLV("DAC Playback Volume", ES8316_DAC_VOLL_REG33,
			ES8316_DAC_VOLR_REG34, 0, 0xC0, 1, dac_vol_tlv),

	SOC_SINGLE("Enable DAC Soft Ramp", ES8316_DAC_SET1_REG30, 4, 1, 1),
	SOC_SINGLE("DAC Soft Ramp Rate", ES8316_DAC_SET1_REG30, 2, 4, 0),

	SOC_ENUM("Playback Polarity", dacpol),
	SOC_SINGLE("DAC Notch Filter", ES8316_DAC_SET2_REG31, 6, 1, 0),
	SOC_SINGLE("DAC Double Fs Mode", ES8316_DAC_SET2_REG31, 7, 1, 0),
	SOC_SINGLE("DAC Volume Control-LeR", ES8316_DAC_SET2_REG31, 2, 1, 0),
	SOC_SINGLE("DAC Stereo Enhancement", ES8316_DAC_SET3_REG32, 0, 7, 0),

	/* +20dB D2SE PGA Control */
	SOC_SINGLE_TLV("MIC Boost", ES8316_ADC_D2SEPGA_REG24,
			0, 1, 0, mic_bst_tlv),
	/* 0-+24dB Lineinput PGA Control */
	SOC_SINGLE_TLV("Input PGA", ES8316_ADC_PGAGAIN_REG23,
			4, 8, 0, linin_pga_tlv),

	/* ADC Digital  Control */
	SOC_SINGLE_TLV("ADC Capture Volume", ES8316_ADC_VOLUME_REG27,
			0, 0xC0, 1, adc_vol_tlv),
	SOC_SINGLE("ADC Soft Ramp", ES8316_ADC_MUTE_REG26, 4, 1, 0),
	SOC_ENUM("Capture Polarity", adcpol),
	SOC_SINGLE("ADC Double FS Mode", ES8316_ADC_DMIC_REG25, 4, 1, 0),
	/* ADC ALC  Control */
	SOC_SINGLE("ALC Capture Target Volume", ES8316_ADC_ALC3_REG2B, 4, 10, 0),
	SOC_SINGLE("ALC Capture Max PGA", ES8316_ADC_ALC1_REG29, 0, 28, 0),
	SOC_SINGLE("ALC Capture Min PGA", ES8316_ADC_ALC2_REG2A, 0, 28, 0),
	SOC_ENUM("ALC Capture Function", alc_func),
	SOC_SINGLE("ALC Capture Hold Time", ES8316_ADC_ALC3_REG2B, 0, 10, 0),
	SOC_SINGLE("ALC Capture Decay Time", ES8316_ADC_ALC4_REG2C, 4, 10, 0),
	SOC_SINGLE("ALC Capture Attack Time", ES8316_ADC_ALC4_REG2C, 0, 10, 0),
	SOC_SINGLE("ALC Capture NG Threshold", ES8316_ADC_ALC6_REG2E, 0, 31, 0),
	SOC_ENUM("ALC Capture NG Type", ng_type),
	SOC_SINGLE("ALC Capture NG Switch", ES8316_ADC_ALC6_REG2E, 5, 1, 0),
};

/* Analog Input MUX */
static const char * const es8316_analog_in_txt[] = {
	"lin1-rin1",
	"lin2-rin2",
	"lin1-rin1 with 20db Boost",
	"lin2-rin2 with 20db Boost"
};
static const unsigned int es8316_analog_in_values[] = {
	0,/*1,*/
	1,
	2,
	3
};
static const struct soc_enum es8316_analog_input_enum =
SOC_VALUE_ENUM_SINGLE(ES8316_ADC_PDN_LINSEL_REG22, 4, 3,
		ARRAY_SIZE(es8316_analog_in_txt),
		es8316_analog_in_txt,
		es8316_analog_in_values);
static const struct snd_kcontrol_new es8316_analog_in_mux_controls =
SOC_DAPM_ENUM("Route", es8316_analog_input_enum);

/* Dmic MUX */
static const char * const es8316_dmic_txt[] = {
	"dmic disable",
	"dmic data at high level",
	"dmic data at low level",
};
static const unsigned int es8316_dmic_values[] = {
	0,/*1,*/
	1,
	2
};
static const struct soc_enum es8316_dmic_src_enum =
SOC_VALUE_ENUM_SINGLE(ES8316_ADC_DMIC_REG25, 0, 3,
		ARRAY_SIZE(es8316_dmic_txt),
		es8316_dmic_txt,
		es8316_dmic_values);
static const struct snd_kcontrol_new es8316_dmic_src_controls =
SOC_DAPM_ENUM("Route", es8316_dmic_src_enum);

/* hp mixer mux */
static const char * const es8316_hpmux_texts[] = {
	"lin1-rin1",
	"lin2-rin2",
	"lin-rin with Boost",
	"lin-rin with Boost and PGA"
};

static const unsigned int es8316_hpmux_values[] = {
	0, 1, 2, 3};

static const struct soc_enum es8316_left_hpmux_enum =
SOC_VALUE_ENUM_SINGLE(ES8316_HPMIX_SEL_REG13, 4, 7,
		ARRAY_SIZE(es8316_hpmux_texts),
		es8316_hpmux_texts,
		es8316_hpmux_values);
static const struct snd_kcontrol_new es8316_left_hpmux_controls =
SOC_DAPM_VALUE_ENUM("Route", es8316_left_hpmux_enum);

static const struct soc_enum es8316_right_hpmux_enum =
SOC_VALUE_ENUM_SINGLE(ES8316_HPMIX_SEL_REG13, 0, 7,
		ARRAY_SIZE(es8316_hpmux_texts),
		es8316_hpmux_texts,
		es8316_hpmux_values);
static const struct snd_kcontrol_new es8316_right_hpmux_controls =
SOC_DAPM_VALUE_ENUM("Route", es8316_right_hpmux_enum);

/* headphone Output Mixer */
static const struct snd_kcontrol_new es8316_out_left_mix[] = {
	SOC_DAPM_SINGLE("LLIN Switch", ES8316_HPMIX_SWITCH_REG14,
			6, 1, 0),
	SOC_DAPM_SINGLE("Left DAC Switch", ES8316_HPMIX_SWITCH_REG14,
			7, 1, 0),
};
static const struct snd_kcontrol_new es8316_out_right_mix[] = {
	SOC_DAPM_SINGLE("RLIN Switch", ES8316_HPMIX_SWITCH_REG14,
			2, 1, 0),
	SOC_DAPM_SINGLE("Right DAC Switch", ES8316_HPMIX_SWITCH_REG14,
			3, 1, 0),
};

/* DAC data source mux */
static const char * const es8316_dacsrc_texts[] = {
	"LDATA TO LDAC, RDATA TO RDAC",
	"LDATA TO LDAC, LDATA TO RDAC",
	"RDATA TO LDAC, RDATA TO RDAC",
	"RDATA TO LDAC, LDATA TO RDAC",
};

static const unsigned int es8316_dacsrc_values[] = {
	0, 1, 2, 3};

static const struct soc_enum es8316_dacsrc_mux_enum =
SOC_VALUE_ENUM_SINGLE(ES8316_DAC_SET1_REG30, 6, 4,
		ARRAY_SIZE(es8316_dacsrc_texts),
		es8316_dacsrc_texts,
		es8316_dacsrc_values);
static const struct snd_kcontrol_new es8316_dacsrc_mux_controls =
SOC_DAPM_VALUE_ENUM("Route", es8316_dacsrc_mux_enum);


static const struct snd_soc_dapm_widget es8316_dapm_widgets[] = {
	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC"),
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_MICBIAS("micbias", ES8316_SYS_PDN_REG0D,
			5, 1),

	/* Input MUX */
	SND_SOC_DAPM_MUX("Differential Mux", SND_SOC_NOPM, 0, 0,
			&es8316_analog_in_mux_controls),
	SND_SOC_DAPM_PGA("Line input PGA", SND_SOC_NOPM,
			0, 0, NULL, 0),

	/* ADCs */
	/* SND_SOC_DAPM_ADC("Mono ADC", NULL, ES8316_ADC_PDN_LINSEL_REG22, 6, 0),*/
	SND_SOC_DAPM_ADC("Mono ADC", NULL, SND_SOC_NOPM, 0, 0),

	/* Dmic MUX */
	SND_SOC_DAPM_MUX("Digital Mic Mux", SND_SOC_NOPM, 0, 0,
			&es8316_dmic_src_controls),

	/* Digital Interface */
	SND_SOC_DAPM_AIF_OUT("I2S OUT", "I2S1 Capture",  1,
			SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("I2S IN", "I2S1 Playback", 0,
			SND_SOC_NOPM, 0, 0),

	/*  DACs DATA SRC MUX */
	SND_SOC_DAPM_MUX("DAC SRC Mux", SND_SOC_NOPM, 0, 0,
			&es8316_dacsrc_mux_controls),
	/*  DACs  */
	SND_SOC_DAPM_DAC("Right DAC", NULL, SND_SOC_NOPM, 0, 0),

	/* SND_SOC_DAPM_DAC("Left DAC", NULL, ES8316_DAC_PDN_REG2F, 4, 0),*/
	SND_SOC_DAPM_DAC("Left DAC", NULL, SND_SOC_NOPM, 0, 0),

	/* Headphone Output Side */
	/* hpmux for hp mixer */
	SND_SOC_DAPM_MUX("Left Hp mux", SND_SOC_NOPM, 0, 0,
			&es8316_left_hpmux_controls),
	SND_SOC_DAPM_MUX("Right Hp mux", SND_SOC_NOPM, 0, 0,
			&es8316_right_hpmux_controls),
	/* Output mixer  */
	SND_SOC_DAPM_MIXER("Left Hp mixer", ES8316_HPMIX_PDN_REG15,
			4, 1, &es8316_out_left_mix[0], ARRAY_SIZE(es8316_out_left_mix)),
	SND_SOC_DAPM_MIXER("Right Hp mixer", ES8316_HPMIX_PDN_REG15,
			0, 1, &es8316_out_right_mix[0], ARRAY_SIZE(es8316_out_right_mix)),


	/* Ouput charge pump */

	SND_SOC_DAPM_PGA("HPCP L", SND_SOC_NOPM,
			0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPCP R", SND_SOC_NOPM,
			0, 0, NULL, 0),

	/* Ouput Driver */
	SND_SOC_DAPM_PGA("HPVOL L", SND_SOC_NOPM,
			0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPVOL R", SND_SOC_NOPM,
			0, 0, NULL, 0),


	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
};

static const struct snd_soc_dapm_route es8316_dapm_routes[] = {
	/*
	 * record route map
	 */
	{"MIC1", NULL, "micbias"},
	{"MIC2", NULL, "micbias"},
	{"DMIC", NULL, "micbias"},

	{"Differential Mux", "lin1-rin1", "MIC1"},
	{"Differential Mux", "lin2-rin2", "MIC2"},
	{"Line input PGA", NULL, "Differential Mux"},

	{"Mono ADC", NULL, "Line input PGA"},

	{"Digital Mic Mux", "dmic disable", "Mono ADC"},
	{"Digital Mic Mux", "dmic data at high level", "DMIC"},
	{"Digital Mic Mux", "dmic data at low level", "DMIC"},

	{"I2S OUT", NULL, "Digital Mic Mux"},
	/*
	 * playback route map
	 */
	{"DAC SRC Mux", "LDATA TO LDAC, RDATA TO RDAC", "I2S IN"},
	{"DAC SRC Mux", "LDATA TO LDAC, LDATA TO RDAC", "I2S IN"},
	{"DAC SRC Mux", "RDATA TO LDAC, RDATA TO RDAC", "I2S IN"},
	{"DAC SRC Mux", "RDATA TO LDAC, LDATA TO RDAC", "I2S IN"},

	{"Left DAC", NULL, "DAC SRC Mux"},
	{"Right DAC", NULL, "DAC SRC Mux"},


	{"Left Hp mux", "lin1-rin1", "MIC1"},
	{"Left Hp mux", "lin2-rin2", "MIC2"},
	{"Left Hp mux", "lin-rin with Boost", "Differential Mux"},
	{"Left Hp mux", "lin-rin with Boost and PGA", "Line input PGA"},

	{"Right Hp mux", "lin1-rin1", "MIC1"},
	{"Right Hp mux", "lin2-rin2", "MIC2"},
	{"Right Hp mux", "lin-rin with Boost", "Differential Mux"},
	{"Right Hp mux", "lin-rin with Boost and PGA", "Line input PGA"},

	{"Left Hp mixer", "LLIN Switch", "Left Hp mux"},
	{"Left Hp mixer", "Left DAC Switch", "Left DAC"},

	{"Right Hp mixer", "RLIN Switch", "Right Hp mux"},
	{"Right Hp mixer", "Right DAC Switch", "Right DAC"},

	{"HPCP L", NULL, "Left Hp mixer"},
	{"HPCP R", NULL, "Right Hp mixer"},

	{"HPVOL L", NULL, "HPCP L"},
	{"HPVOL R", NULL, "HPCP R"},

	{"HPOL", NULL, "HPVOL L"},
	{"HPOR", NULL, "HPVOL R"},
};

struct _coeff_div {
	u32 mclk;       /* mclk frequency */
	u32 rate;       /* sample rate */
	u8 div;         /* adcclk and dacclk divider */
	u8 lrck_h;      /* adclrck divider and daclrck divider */
	u8 lrck_l;
	u8 sr;          /* sclk divider */
	u8 osr;         /* adc osr */
};


/* codec hifi mclk clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
	/* 8k */
	{12288000, 8000 , 6 , 0x06, 0x00, 21, 32},
	{11289600, 8000 , 6 , 0x05, 0x83, 20, 29},
	{18432000, 8000 , 9 , 0x09, 0x00, 27, 32},
	{16934400, 8000 , 8 , 0x08, 0x44, 25, 33},
	{12000000, 8000 , 7 , 0x05, 0xdc, 21, 25},
	{19200000, 8000 , 12, 0x09, 0x60, 27, 25},

	/* 11.025k */
	{11289600, 11025, 4 , 0x04, 0x00, 16, 32},
	{16934400, 11025, 6 , 0x06, 0x00, 21, 32},
	{12000000, 11025, 4 , 0x04, 0x40, 17, 34},

	/* 16k */
	{12288000, 16000, 3 , 0x03, 0x00, 12, 32},
	{18432000, 16000, 5 , 0x04, 0x80, 18, 25},
	{12000000, 16000, 3 , 0x02, 0xee, 12, 31},
	{19200000, 16000, 6 , 0x04, 0xb0, 18, 25},

	/* 22.05k */
	{11289600, 22050, 2 , 0x02, 0x00, 8 , 32},
	{16934400, 22050, 3 , 0x03, 0x00, 12, 32},
	{12000000, 22050, 2 , 0x02, 0x20, 8 , 34},

	/* 32k */
	{12288000, 32000, 1 , 0x01, 0x80, 6 , 48},
	{18432000, 32000, 2 , 0x02, 0x40, 9 , 32},
	{12000000, 32000, 1 , 0x01, 0x77, 6 , 31},
	{19200000, 32000, 3 , 0x02, 0x58, 10, 25},

	/* 44.1k */
	{11289600, 44100, 1 , 0x01, 0x00, 4 , 32},
	{16934400, 44100, 1 , 0x01, 0x80, 6 , 32},
	{12000000, 44100, 1 , 0x01, 0x10, 4 , 34},

	/* 48k */
	{12288000, 48000, 1 , 0x01, 0x00, 4 , 32},
	{18432000, 48000, 1 , 0x01, 0x80, 6 , 32},
	{12000000, 48000, 1 , 0x00, 0xfa, 4 , 31},
	{19200000, 48000, 2 , 0x01, 0x90, 6, 25},

	/* 88.2k */
	{11289600, 88200, 1 , 0x00, 0x80, 2 , 32},
	{16934400, 88200, 1 , 0x00, 0xc0, 3 , 48},
	{12000000, 88200, 1 , 0x00, 0x88, 2 , 34},

	/* 96k */
	{12288000, 96000, 1 , 0x00, 0x80, 2 , 32},
	{18432000, 96000, 1 , 0x00, 0xc0, 3 , 48},
	{12000000, 96000, 1 , 0x00, 0x7d, 1 , 31},
	{19200000, 96000, 1 , 0x00, 0xc8, 3 , 25},
};
static inline int get_coeff(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
			return i;
	}

	return -EINVAL;
}

/* The set of rates we can generate from the above for each SYSCLK */

static unsigned int rates_12288[] = {
	8000, 12000, 16000, 24000, 24000, 32000, 48000, 96000,
};

static struct snd_pcm_hw_constraint_list constraints_12288 = {
	.count	= ARRAY_SIZE(rates_12288),
	.list	= rates_12288,
};

static unsigned int rates_112896[] = {
	8000, 11025, 22050, 44100,
};

static struct snd_pcm_hw_constraint_list constraints_112896 = {
	.count	= ARRAY_SIZE(rates_112896),
	.list	= rates_112896,
};

static unsigned int rates_12[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
	48000, 88235, 96000,
};

static struct snd_pcm_hw_constraint_list constraints_12 = {
	.count	= ARRAY_SIZE(rates_12),
	.list	= rates_12,
};

/*
 * Note that this should be called from init rather than from hw_params.
 */
static int es8316_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct es8316_priv *es8316 = snd_soc_codec_get_drvdata(codec);

	DBG("Enter::%s----%d, freq:%ld\n", __func__, __LINE__, freq);

	switch (freq) {
	case 11289600:
	case 18432000:
	case 22579200:
	case 36864000:
		es8316->sysclk_constraints = &constraints_112896;
		es8316->sysclk = freq;
		return 0;

	case 12288000:
	case 19200000:
	case 16934400:
	case 24576000:
	case 33868800:
		es8316->sysclk_constraints = &constraints_12288;
		es8316->sysclk = freq;
		return 0;

	case 12000000:
	case 24000000:
		es8316->sysclk_constraints = &constraints_12;
		es8316->sysclk = freq;
		return 0;
	}

	return -EINVAL;
}

static int es8316_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u8 iface = 0;
	u8 adciface = 0;
	u8 daciface = 0;

	alsa_dbg("%s----%d, fmt[%02x]\n", __func__, __LINE__, fmt);

	iface    = snd_soc_read(codec, ES8316_IFACE);
	adciface = snd_soc_read(codec, ES8316_ADC_IFACE);
	daciface = snd_soc_read(codec, ES8316_DAC_IFACE);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:    /* MASTER MODE */
		alsa_dbg("es8316 in master mode");
		iface |= 0x80;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:    /* SLAVE MODE */
		alsa_dbg("es8316 in slave mode");
		iface &= 0x7F;
		break;
	default:
		return -EINVAL;
	}


	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		adciface &= 0xFC;
		daciface &= 0xFC;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		return -EINVAL;
	case SND_SOC_DAIFMT_LEFT_J:
		adciface &= 0xFC;
		daciface &= 0xFC;
		adciface |= 0x01;
		daciface |= 0x01;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		adciface &= 0xDC;
		daciface &= 0xDC;
		adciface |= 0x03;
		daciface |= 0x03;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		adciface &= 0xDC;
		daciface &= 0xDC;
		adciface |= 0x23;
		daciface |= 0x23;
		break;
	default:
		return -EINVAL;
	}


	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		iface    &= 0xDF;
		adciface &= 0xDF;
		daciface &= 0xDF;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface    |= 0x20;
		adciface |= 0x20;
		daciface |= 0x20;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface    |= 0x20;
		adciface &= 0xDF;
		daciface &= 0xDF;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface    &= 0xDF;
		adciface |= 0x20;
		daciface |= 0x20;
		break;
	default:
		return -EINVAL;
	}
	snd_soc_write(codec, ES8316_IFACE, iface);
	snd_soc_write(codec, ES8316_ADC_IFACE, adciface);
	snd_soc_write(codec, ES8316_DAC_IFACE, daciface);

	return 0;
}

static int es8316_pcm_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct es8316_priv *es8316 = snd_soc_codec_get_drvdata(codec);

	DBG("Enter::%s----%d  es8316->sysclk=%d\n", __func__, __LINE__, es8316->sysclk);

	/* The set of sample rates that can be supported depends on the
	 * MCLK supplied to the CODEC - enforce this.
	 */
	if (!es8316->sysclk) {
		dev_err(codec->dev,
				"No MCLK configured, call set_sysclk() on init\n");
		return -EINVAL;
	}

	snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE,
			es8316->sysclk_constraints);

	return 0;
}
static int es8316_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct es8316_priv *es8316 = snd_soc_codec_get_drvdata(codec);
	u16 osrate  =  snd_soc_read(codec, ES8316_CLKMGR_ADCOSR_REG03) & 0xc0;
	u16 mclkdiv = snd_soc_read(codec, ES8316_CLKMGR_CLKSW_REG01) & 0x7f;
	u16 srate = snd_soc_read(codec, ES8316_SDP_MS_BCKDIV_REG09) & 0xE0;
	u16 adciface = snd_soc_read(codec, ES8316_SDP_ADCFMT_REG0A) & 0xE3;
	u16 daciface = snd_soc_read(codec, ES8316_SDP_DACFMT_REG0B) & 0xE3;
	u16 adcdiv   = snd_soc_read(codec, ES8316_CLKMGR_ADCDIV1_REG04);
	u16 adclrckdiv_l = snd_soc_read(codec, ES8316_CLKMGR_ADCDIV2_REG05) & 0x00;
	u16 dacdiv   = snd_soc_read(codec, ES8316_CLKMGR_DACDIV1_REG06);
	u16 daclrckdiv_l = snd_soc_read(codec, ES8316_CLKMGR_DACDIV2_REG07) & 0x00;
	int coeff;
	u16 adclrckdiv_h = adcdiv & 0xf0;
	u16 daclrckdiv_h = dacdiv & 0xf0;
	adcdiv &= 0x0f;
	dacdiv &= 0x0f;
	int retv;

	coeff = get_coeff(es8316->sysclk, params_rate(params));
	if (coeff < 0) {
		coeff = get_coeff(es8316->sysclk / 2, params_rate(params));
		mclkdiv |= 0x80;
	}
	if (coeff < 0) {
		dev_err(codec->dev,
				"Unable to configure sample rate %dHz with %dHz MCLK\n",
				params_rate(params), es8316->sysclk);
		return coeff;
	}

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		adciface |= 0x000C;
		daciface |= 0x000C;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		adciface |= 0x0004;
		daciface |= 0x0004;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		adciface |= 0x0010;
		daciface |= 0x0010;
		break;
	}

	/* set iface & srate*/
	snd_soc_update_bits(codec, ES8316_SDP_DACFMT_REG0B, 0xe3, daciface);
	snd_soc_update_bits(codec, ES8316_SDP_ADCFMT_REG0A, 0xe3, adciface);
	snd_soc_update_bits(codec, ES8316_CLKMGR_CLKSW_REG01, 0x80, mclkdiv);
	if (coeff >= 0) {
		osrate = coeff_div[coeff].osr;
		osrate &= 0x3f;

		srate |= coeff_div[coeff].sr;
		srate &= 0x1f;

		adcdiv |= (coeff_div[coeff].div << 4);
		adclrckdiv_h |= coeff_div[coeff].lrck_h;
		adcdiv &= 0xf0;
		adclrckdiv_h &= 0x0f;
		adcdiv |= adclrckdiv_h;
		adclrckdiv_l = coeff_div[coeff].lrck_l;

		dacdiv |= (coeff_div[coeff].div << 4);
		daclrckdiv_h |= coeff_div[coeff].lrck_h;
		dacdiv &= 0xf0;
		daclrckdiv_h &= 0x0f;
		dacdiv |= daclrckdiv_h;
		daclrckdiv_l = coeff_div[coeff].lrck_l;
		snd_soc_update_bits(codec, ES8316_CLKMGR_ADCOSR_REG03, 0x3f, osrate);
		snd_soc_update_bits(codec, ES8316_SDP_MS_BCKDIV_REG09, 0x1f, srate);
		snd_soc_update_bits(codec, ES8316_CLKMGR_ADCDIV1_REG04, 0xff, adcdiv);
		snd_soc_update_bits(codec, ES8316_CLKMGR_ADCDIV2_REG05, 0xff, adclrckdiv_l);
		snd_soc_update_bits(codec, ES8316_CLKMGR_DACDIV1_REG06, 0xff, dacdiv);
		snd_soc_update_bits(codec, ES8316_CLKMGR_DACDIV2_REG07, 0xff, daclrckdiv_l);
		/*
		 * must do codec power on initialization at here becauses
		 * MCLK and I2S CLK always startup at here
		 */
		if (es8316_init_reg < 1) {
			es8316_init_regs(codec);
			if (es8316->dmic_amic ==  dmic_used)
				/* set gpio2 to DMIC CLK */
				snd_soc_write(codec, ES8316_GPIO_SEL_REG4D, 0x02);
			else
				/* set gpio2 to GM SHORT */
				snd_soc_write(codec, ES8316_GPIO_SEL_REG4D, 0x00);
			/* maximum debance time, enable interrupt, low active */
			snd_soc_write(codec, ES8316_GPIO_DEBUNCE_INT_REG4E, 0xf3);
			es8316_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
			es8316_init_reg = 1;
		}
	}

	return 0;
}

static int es8316_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;

	dev_dbg(codec->dev, "%s %d\n", __func__, mute);
	if (mute) {
		snd_soc_write(codec, ES8316_DAC_SET1_REG30, 0x20);
	} else {
		if (dai->playback_active)
			snd_soc_write(codec, ES8316_DAC_SET1_REG30, 0x00);
	}
	return 0;
}

static int es8316_set_bias_level(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level)
{
	return 0;
	switch (level) {
	case SND_SOC_BIAS_ON:
		dev_dbg(codec->dev, "%s on\n", __func__);
		break;
	case SND_SOC_BIAS_PREPARE:
		dev_dbg(codec->dev, "%s prepare\n", __func__);
		if (es8316_init_reg > 0) {
			snd_soc_write(codec, ES8316_CLKMGR_CLKSW_REG01, 0x7F);
			snd_soc_write(codec, ES8316_SYS_PDN_REG0D, 0x00);
			snd_soc_write(codec, ES8316_ADC_PDN_LINSEL_REG22, 0x00);
			snd_soc_write(codec, ES8316_DAC_PDN_REG2F, 0x00);
			snd_soc_write(codec, ES8316_HPMIX_SWITCH_REG14, 0x88);
			snd_soc_write(codec, ES8316_HPMIX_PDN_REG15, 0x88);
			snd_soc_write(codec, ES8316_HPMIX_VOL_REG16, 0xBB);
			snd_soc_write(codec, ES8316_CPHP_PDN2_REG1A, 0x10);
			snd_soc_write(codec, ES8316_CPHP_LDOCTL_REG1B, 0x30);
			snd_soc_write(codec, ES8316_CPHP_PDN1_REG19, 0x02);
			snd_soc_write(codec, ES8316_CPHP_ICAL_VOL_REG18, 0x00);
			snd_soc_write(codec, ES8316_RESET_REG00, 0xC0);
			snd_soc_write(codec, ES8316_CPHP_OUTEN_REG17, 0x66);
		}
		break;
	case SND_SOC_BIAS_STANDBY:
		dev_dbg(codec->dev, "%s standby\n", __func__);
		if (es8316_init_reg > 0) {   /* if codec intialized, */
			snd_soc_write(codec, ES8316_CLKMGR_CLKSW_REG01, 0x7F);
			snd_soc_write(codec, ES8316_SYS_PDN_REG0D, 0x00);
			snd_soc_write(codec, ES8316_ADC_PDN_LINSEL_REG22, 0x00);
			snd_soc_write(codec, ES8316_DAC_PDN_REG2F, 0x00);
			snd_soc_write(codec, ES8316_HPMIX_SWITCH_REG14, 0x88);
			snd_soc_write(codec, ES8316_HPMIX_PDN_REG15, 0x88);
			snd_soc_write(codec, ES8316_HPMIX_VOL_REG16, 0xBB);
			snd_soc_write(codec, ES8316_CPHP_PDN2_REG1A, 0x10);
			snd_soc_write(codec, ES8316_CPHP_LDOCTL_REG1B, 0x30);
			snd_soc_write(codec, ES8316_CPHP_PDN1_REG19, 0x02);
			snd_soc_write(codec, ES8316_CPHP_ICAL_VOL_REG18, 0x00);
			snd_soc_write(codec, ES8316_RESET_REG00, 0xC0);
			snd_soc_write(codec, ES8316_CPHP_OUTEN_REG17, 0x66);
		}
		break;
	case SND_SOC_BIAS_OFF:
		dev_dbg(codec->dev, "%s off\n", __func__);
		snd_soc_write(codec, ES8316_CPHP_ICAL_VOL_REG18, 0x33);
		snd_soc_write(codec, ES8316_CPHP_OUTEN_REG17, 0x00);
		snd_soc_write(codec, ES8316_CPHP_LDOCTL_REG1B, 0x03);
		snd_soc_write(codec, ES8316_CPHP_PDN2_REG1A, 0x22);
		snd_soc_write(codec, ES8316_CPHP_PDN1_REG19, 0x06);
		snd_soc_write(codec, ES8316_HPMIX_SWITCH_REG14, 0x00);
		snd_soc_write(codec, ES8316_HPMIX_PDN_REG15, 0x33);
		snd_soc_write(codec, ES8316_HPMIX_VOL_REG16, 0x00);
		snd_soc_write(codec, ES8316_ADC_PDN_LINSEL_REG22, 0xC0);
		snd_soc_write(codec, ES8316_DAC_PDN_REG2F, 0x11);
		snd_soc_write(codec, ES8316_SYS_PDN_REG0D, 0x3F);
		snd_soc_write(codec, ES8316_CLKMGR_CLKSW_REG01, 0x03);
		snd_soc_write(codec, ES8316_RESET_REG00, 0x7F);
		break;
	}
	codec->dapm.bias_level = level;

	return 0;
}

#define es8316_RATES SNDRV_PCM_RATE_8000_96000

#define es8316_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
		SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops es8316_ops = {
	/*.startup = es8316_pcm_startup, */
	.hw_params = es8316_pcm_hw_params,
	.set_fmt = es8316_set_dai_fmt,
	.set_sysclk = es8316_set_dai_sysclk,
	.digital_mute = es8316_mute,
};

static struct snd_soc_dai_driver es8316_dai[] = {
{
	.name = "ES8316 HiFi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = es8316_RATES,
		.formats = es8316_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = es8316_RATES,
		.formats = es8316_FORMATS,
	},
	.ops = &es8316_ops,
	.symmetric_rates = 1,
},
};


static int es8316_init_regs(struct snd_soc_codec *codec)
{
	/* setup adc volume */
	snd_soc_write(codec, ES8316_ADC_VOLUME_REG27, 0x00);
	/* disable DAC ds and notch mode */
	snd_soc_write(codec, ES8316_DAC_SET2_REG31, 0x00);
	/* set dac volume */
	snd_soc_write(codec, ES8316_DAC_VOLL_REG33, 0x00);
	snd_soc_write(codec, ES8316_DAC_VOLR_REG34, 0x00);
	/* ADC FORMAT , i2s-16bit */
	snd_soc_write(codec, ES8316_SDP_ADCFMT_REG0A, 0x0C);
	/* DAC FORMAT, i2s-16bit */
	snd_soc_write(codec, ES8316_SDP_DACFMT_REG0B, 0x0C);
	snd_soc_write(codec, ES8316_CLKMGR_CLKSEL_REG02, 0x09);
	/* CHARGE PUMP DIVIDER */
	snd_soc_write(codec, 0x03, 0x19);
	snd_soc_write(codec, 0x04, 0x21);
	snd_soc_write(codec, 0x05, 0x90);
	snd_soc_write(codec, 0x06, 0x21);
	snd_soc_write(codec, 0x07, 0x90);
	snd_soc_write(codec, 0x09, 0x06);
	/* CHARGE PUMP DIVIDER */
	snd_soc_write(codec, ES8316_CLKMGR_CPDIV_REG08, 0x00);
	/* CLK ON */
	snd_soc_write(codec, ES8316_CLKMGR_CLKSW_REG01, 0x7F);
	/* VMID SEQUENCE */
	snd_soc_write(codec, ES8316_SYS_VMIDSEL_REG0C, 0xFa);
	/* ADC,DAC VREF SET, micbias pup */
	snd_soc_write(codec, ES8316_SYS_PDN_REG0D, 0x00);
	/* CSM POWER UP */
	snd_soc_write(codec, ES8316_RESET_REG00, 0xC0);
	msleep(50);
	/* INPUT SELECT INPUT1:0X20,INPUT2:0X30, ain and adc power down
	 * snd_soc_write(codec, ES8316_ADC_PDN_LINSEL_REG22, 0x10); */
	/* POWER UP DACPOWER */
	snd_soc_write(codec, ES8316_DAC_PDN_REG2F, 0x00);
	/* L,R MIXER SET */
	snd_soc_write(codec, ES8316_HPMIX_SEL_REG13, 0x00);
	snd_soc_write(codec, ES8316_HPMIX_SWITCH_REG14, 0x88);
	snd_soc_write(codec, ES8316_HPMIX_PDN_REG15, 0x00);
	snd_soc_write(codec, ES8316_HPMIX_VOL_REG16, 0xBB);
	/* CHARGE PUMP */
	snd_soc_write(codec, ES8316_CPHP_PDN2_REG1A, 0x10);
	snd_soc_write(codec, ES8316_CPHP_LDOCTL_REG1B, 0x00);
	snd_soc_write(codec, ES8316_CPHP_PDN1_REG19, 0x03);
	snd_soc_write(codec, ES8316_CPHP_ICAL_VOL_REG18, 0x00);
	/* vmidlow = 10'b */
	snd_soc_write(codec, ES8316_SYS_VMIDLOW_REG10, 0x09);
	msleep(20);
	/* HPOUT SET */
	snd_soc_write(codec, ES8316_CPHP_OUTEN_REG17, 0x66);

	snd_soc_write(codec, ES8316_GPIO_DEBUNCE_INT_REG4E, 0xf3);
	snd_soc_write(codec, ES8316_TESTMODE_REG50, 0xA0);
	snd_soc_write(codec, ES8316_TEST2_REG52, 0x03);
	/*alc set*/
	snd_soc_write(codec, ES8316_ADC_PGAGAIN_REG23, 0xa0);
	snd_soc_write(codec, ES8316_ADC_D2SEPGA_REG24, 0x01);
	/* adc ds mode, HPF enable */
	snd_soc_write(codec, ES8316_ADC_DMIC_REG25, 0x08);
	/* ALC ON */
	snd_soc_write(codec, ES8316_ADC_ALC1_REG29, 0xcd);
	snd_soc_write(codec, ES8316_ADC_ALC2_REG2A, 0x08);
	snd_soc_write(codec, ES8316_ADC_ALC3_REG2B, 0xa0);
	snd_soc_write(codec, ES8316_ADC_ALC4_REG2C, 0x05);
	snd_soc_write(codec, ES8316_ADC_ALC5_REG2D, 0x06);
	/* NOISE GATE ENABLE, GATE=-75db */
	snd_soc_write(codec, ES8316_ADC_ALC6_REG2E, 0x61);

	return 0;
}

static int es8316_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	snd_soc_write(codec, ES8316_CPHP_ICAL_VOL_REG18, 0x33);
	snd_soc_write(codec, ES8316_CPHP_OUTEN_REG17, 0x00);
	snd_soc_write(codec, ES8316_CPHP_LDOCTL_REG1B, 0x03);
	snd_soc_write(codec, ES8316_CPHP_PDN2_REG1A, 0x22);
	snd_soc_write(codec, ES8316_CPHP_PDN1_REG19, 0x06);
	snd_soc_write(codec, ES8316_HPMIX_SWITCH_REG14, 0x00);
	snd_soc_write(codec, ES8316_HPMIX_PDN_REG15, 0x33);
	snd_soc_write(codec, ES8316_HPMIX_VOL_REG16, 0x00);
	snd_soc_write(codec, ES8316_ADC_PDN_LINSEL_REG22, 0xC0);
	snd_soc_write(codec, ES8316_CLKMGR_CLKSW_REG01, 0x03);
	return 0;
}

static int es8316_resume(struct snd_soc_codec *codec)
{
	snd_soc_write(codec, ES8316_CPHP_ICAL_VOL_REG18, 0x00);
	snd_soc_write(codec, ES8316_CPHP_LDOCTL_REG1B, 0x30);
	snd_soc_write(codec, ES8316_CPHP_PDN2_REG1A, 0x10);
	snd_soc_write(codec, ES8316_CPHP_PDN1_REG19, 0x02);
	snd_soc_write(codec, ES8316_HPMIX_SWITCH_REG14, 0x88);
	snd_soc_write(codec, ES8316_HPMIX_PDN_REG15, 0x88);
	snd_soc_write(codec, ES8316_HPMIX_VOL_REG16, 0xbb);
	snd_soc_write(codec, ES8316_ADC_PDN_LINSEL_REG22, 0x20);
	snd_soc_write(codec, ES8316_CLKMGR_CLKSW_REG01, 0x7f);
	snd_soc_write(codec, ES8316_CPHP_OUTEN_REG17, 0x66);
	return 0;
}

static int es8316_probe(struct snd_soc_codec *codec)
{
	int ret = 0;
	struct es8316_priv *es8316 = snd_soc_codec_get_drvdata(codec);

	ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_I2C);
	if (ret < 0)
		return ret;

	if (ret < 0) {
		dev_err(codec->dev, "fail to reset audio (%d)\n", ret);
		goto err;
	}
	snd_soc_write(codec, ES8316_GPIO_DEBUNCE_INT_REG4E, 0xf0);
	snd_soc_write(codec, 0x01, 0x7f);
	snd_soc_write(codec, 0x00, 0xc3);
	msleep(100);
	if (es8316->dmic_amic == dmic_used)
		snd_soc_write(codec, ES8316_GPIO_SEL_REG4D, 0x02);  /* set gpio2 to DMIC CLK */
	else
		snd_soc_write(codec, ES8316_GPIO_SEL_REG4D, 0x00);  /* set gpio2 to GM SHORT */
	snd_soc_write(codec, ES8316_GPIO_DEBUNCE_INT_REG4E, 0xf3);  /*maximum debance time, enable interrupt, low active */

	codec->dapm.idle_bias_off = 0;
#if defined(HS_IRQ)
	det_initalize();
#elif defined(HS_TIMER)
	hsdet_init();
#endif
err:
	return ret;
}

static int es8316_remove(struct snd_soc_codec *codec)
{
	es8316_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_es8316 = {
	.probe =	es8316_probe,
	.remove =	es8316_remove,
	.suspend =	es8316_suspend,
	.resume =	es8316_resume,
	.set_bias_level = es8316_set_bias_level,

	.reg_cache_size = ARRAY_SIZE(es8316_reg_defaults),
	.reg_word_size = sizeof(u8),
	.reg_cache_default = es8316_reg_defaults,
	.volatile_register = es8316_volatile,
	.readable_register = es8316_readable,

	.controls = es8316_snd_controls,
	.num_controls = ARRAY_SIZE(es8316_snd_controls),
	.dapm_widgets = es8316_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(es8316_dapm_widgets),
	.dapm_routes = es8316_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(es8316_dapm_routes),
};

#if defined(CONFIG_SPI_MASTER)
static int es8316_spi_probe(struct spi_device *spi)
{
	struct es8316_priv *es8316;
	int ret;

	es8316 = kzalloc(sizeof(struct es8316_priv), GFP_KERNEL);
	if (es8316 == NULL)
		return -ENOMEM;

	spi_set_drvdata(spi, es8316);

	ret = snd_soc_register_codec(&spi->dev,
			&soc_codec_dev_es8316, &es8316_dai, 1);
	if (ret < 0)
		kfree(es8316);
	return ret;
}

static int es8316_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	kfree(spi_get_drvdata(spi));
	return 0;
}

static struct spi_driver es8316_spi_driver = {
	.driver = {
		.name	= "es8316",
		.owner	= THIS_MODULE,
	},
	.probe	= es8316_spi_probe,
	.remove	= es8316_spi_remove,
};
#endif /* CONFIG_SPI_MASTER */

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)

static void es8316_i2c_shutdown(struct i2c_client *i2c)
{
	struct snd_soc_codec *codec;

	if (!es8316_codec)
		goto err;
	codec = es8316_codec;
	snd_soc_write(codec, ES8316_CPHP_ICAL_VOL_REG18, 0x33);
	snd_soc_write(codec, ES8316_CPHP_OUTEN_REG17, 0x00);
	snd_soc_write(codec, ES8316_CPHP_LDOCTL_REG1B, 0x03);
	snd_soc_write(codec, ES8316_CPHP_PDN2_REG1A, 0x22);
	snd_soc_write(codec, ES8316_CPHP_PDN1_REG19, 0x06);
	snd_soc_write(codec, ES8316_HPMIX_SWITCH_REG14, 0x00);
	snd_soc_write(codec, ES8316_HPMIX_PDN_REG15, 0x33);
	snd_soc_write(codec, ES8316_HPMIX_VOL_REG16, 0x00);
	snd_soc_write(codec, ES8316_ADC_PDN_LINSEL_REG22, 0xC0);
	snd_soc_write(codec, ES8316_DAC_PDN_REG2F, 0x11);
	snd_soc_write(codec, ES8316_SYS_PDN_REG0D, 0x3F);
	snd_soc_write(codec, ES8316_CLKMGR_CLKSW_REG01, 0x03);
	snd_soc_write(codec, ES8316_RESET_REG00, 0x7F);
err:
	return;
}

static int es8316_i2c_probe(struct i2c_client *i2c_client,
		const struct i2c_device_id *id)
{
	struct es8316_priv *es8316;

	int ret = -1;

	es8316 = kzalloc(sizeof(*es8316), GFP_KERNEL);
	if (es8316 == NULL)
		return -ENOMEM;

	es8316->dmic_amic = amic_used;     /* if internal mic is amic */

	i2c_set_clientdata(i2c_client, es8316);
	ret =  snd_soc_register_codec(&i2c_client->dev, &soc_codec_dev_es8316,
			&es8316_dai, ARRAY_SIZE(es8316_dai));
	if (ret < 0) {
		kfree(es8316);
		return ret;
	}

	return ret;
}

static  int es8316_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id es8316_i2c_id[] = {
	{"es8316", 0 },
	{"10ES8316:00", 0},
	{"10ES8316", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, es8316_i2c_id);

static struct i2c_driver es8316_i2c_driver = {
	.driver = {
		.name	= "es8316",
		.owner	= THIS_MODULE,
	},
	.shutdown	= es8316_i2c_shutdown,
	.probe	= es8316_i2c_probe,
	.remove	= es8316_i2c_remove,
	.id_table	= es8316_i2c_id,
};
#endif

static int __init es8316_init(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	return i2c_add_driver(&es8316_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	return spi_register_driver(&es8316_spi_driver);
#endif
}

static void __exit es8316_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	return i2c_del_driver(&es8316_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	return spi_unregister_driver(&es8316_spi_driver);
#endif
}

module_init(es8316_init);
module_exit(es8316_exit);

MODULE_DESCRIPTION("ASoC es8316 driver");
MODULE_AUTHOR("Will <will@everset-semi.com>");
MODULE_LICENSE("GPL");