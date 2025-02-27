/*
 * drivers/amlogic/media/vout/hdmitx/hdmi_tx_20/hdmi_tx_edid.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <crypto/hash.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/delay.h>

#include <linux/amlogic/media/vout/vinfo.h>
#include <linux/amlogic/media/vout/vout_notify.h>
#include <linux/amlogic/media/vout/hdmi_tx/hdmi_info_global.h>
#include <linux/amlogic/media/vout/hdmi_tx/hdmi_tx_module.h>
#include "hw/common.h"

#define CEA_DATA_BLOCK_COLLECTION_ADDR_1StP 0x04
#define VIDEO_TAG 0x40
#define AUDIO_TAG 0x20
#define VENDOR_TAG 0x60
#define SPEAKER_TAG 0x80

#define HDMI_EDID_BLOCK_TYPE_RESERVED	        0
#define HDMI_EDID_BLOCK_TYPE_AUDIO		1
#define HDMI_EDID_BLOCK_TYPE_VIDEO		2
#define HDMI_EDID_BLOCK_TYPE_VENDER	        3
#define HDMI_EDID_BLOCK_TYPE_SPEAKER	        4
#define HDMI_EDID_BLOCK_TYPE_VESA		5
#define HDMI_EDID_BLOCK_TYPE_RESERVED2	        6
#define HDMI_EDID_BLOCK_TYPE_EXTENDED_TAG       7

#define EXTENSION_VENDOR_SPECIFIC 0x1
#define EXTENSION_COLORMETRY_TAG 0x5
/* DRM stands for "Dynamic Range and Mastering " */
#define EXTENSION_DRM_STATIC_TAG    0x6
#define EXTENSION_DRM_DYNAMIC_TAG   0x7
   #define TYPE_1_HDR_METADATA_TYPE    0x0001
   #define TS_103_433_SPEC_TYPE        0x0002
   #define ITU_T_H265_SPEC_TYPE        0x0003
   #define TYPE_4_HDR_METADATA_TYPE    0x0004
/* Video Format Preference Data block */
#define EXTENSION_VFPDB_TAG	0xd
#define EXTENSION_Y420_VDB_TAG	0xe
#define EXTENSION_Y420_CMDB_TAG	0xf

#define EDID_DETAILED_TIMING_DES_BLOCK0_POS 0x36
#define EDID_DETAILED_TIMING_DES_BLOCK1_POS 0x48
#define EDID_DETAILED_TIMING_DES_BLOCK2_POS 0x5A
#define EDID_DETAILED_TIMING_DES_BLOCK3_POS 0x6C

/* EDID Descrptor Tag */
#define TAG_PRODUCT_SERIAL_NUMBER 0xFF
#define TAG_ALPHA_DATA_STRING 0xFE
#define TAG_RANGE_LIMITS 0xFD
#define TAG_DISPLAY_PRODUCT_NAME_STRING 0xFC /* MONITOR NAME */
#define TAG_COLOR_POINT_DATA 0xFB
#define TAG_STANDARD_TIMINGS 0xFA
#define TAG_DISPLAY_COLOR_MANAGEMENT 0xF9
#define TAG_CVT_TIMING_CODES 0xF8
#define TAG_ESTABLISHED_TIMING_III 0xF7
#define TAG_DUMMY_DES 0x10

static unsigned char __nosavedata edid_checkvalue[4] = {0};
static unsigned int hdmitx_edid_check_valid_blocks(unsigned char *buf);
static void Edid_DTD_parsing(struct rx_cap *prxcap, unsigned char *data);
static void hdmitx_edid_set_default_aud(struct hdmitx_dev *hdev);

static int xtochar(int num, unsigned char *checksum)
{
	if (((edid_checkvalue[num]  >> 4) & 0xf) <= 9)
		checksum[0] = ((edid_checkvalue[num]  >> 4) & 0xf) + '0';
	else
		checksum[0] = ((edid_checkvalue[num]  >> 4) & 0xf) - 10 + 'a';

	if ((edid_checkvalue[num] & 0xf) <= 9)
		checksum[1] = (edid_checkvalue[num] & 0xf) + '0';
	else
		checksum[1] = (edid_checkvalue[num] & 0xf) - 10 + 'a';

	return 0;
}

static void edid_save_checkvalue(unsigned char *buf, unsigned int block_cnt,
	struct rx_cap *rxcap)
{
	unsigned int i, length, max;

	if (buf == NULL)
		return;

	length = sizeof(edid_checkvalue);
	memset(edid_checkvalue, 0x00, length);

	max = (block_cnt > length)?length:block_cnt;

	for (i = 0; i < max; i++)
		edid_checkvalue[i] = *(buf+(i+1)*128-1);

	rxcap->chksum[0] = '0';
	rxcap->chksum[1] = 'x';

	for (i = 0; i < 4; i++)
		xtochar(i, &rxcap->chksum[2 * i + 2]);
}

static int Edid_DecodeHeader(struct hdmitx_info *info, unsigned char *buff)
{
	int i, ret = 0;

	if (!(buff[0] | buff[7])) {
		for (i = 1; i < 7; i++) {
			if (buff[i] != 0xFF)
				ret = -1;
		}
	} else
		ret = -1;
	return ret;
}

static void Edid_ParsingIDManufacturerName(struct rx_cap *prxcap,
					   unsigned char *data)
{
	int i;
	unsigned char uppercase[26] = { 0 };
	unsigned char brand[3];

	/* Fill array uppercase with 'A' to 'Z' */
	for (i = 0; i < 26; i++)
		uppercase[i] = 'A' + i;

	brand[0] = data[0] >> 2;
	brand[1] = ((data[0] & 0x3) << 3) + (data[1] >> 5);
	brand[2] = data[1] & 0x1f;

	if (((brand[0] > 26) || (brand[0] == 0))
		|| ((brand[1] > 26) || (brand[1] == 0))
		|| ((brand[2] > 26) || (brand[2] == 0)))
		return;
	for (i = 0; i < 3; i++)
		prxcap->IDManufacturerName[i] = uppercase[brand[i] - 1];
}

static void Edid_ParsingIDProductCode(struct rx_cap *prxcap,
				      unsigned char *data)
{
	if (data == NULL)
		return;
	prxcap->IDProductCode[0] = data[1];
	prxcap->IDProductCode[1] = data[0];
}

static void Edid_ParsingIDSerialNumber(struct rx_cap *prxcap,
				       unsigned char *data)
{
	int i;

	if (data != NULL)
		for (i = 0; i < 4; i++)
			prxcap->IDSerialNumber[i] = data[3 - i];
}

/* store the idx of vesa_timing[32], which is 0 */
static void store_vesa_idx(struct rx_cap *prxcap, enum hdmi_vic vesa_timing)
{
	int i;

	for (i = 0; i < VESA_MAX_TIMING; i++) {
		if (!prxcap->vesa_timing[i]) {
			prxcap->vesa_timing[i] = vesa_timing;
			break;
		}

		if (prxcap->vesa_timing[i] == vesa_timing)
			break;
	}
}

static void Edid_EstablishedTimings(struct rx_cap *prxcap, unsigned char *data)
{
	if (data[0] & (1 << 5))
		store_vesa_idx(prxcap, HDMIV_640x480p60hz);
	if (data[0] & (1 << 0))
		store_vesa_idx(prxcap, HDMIV_800x600p60hz);
	if (data[1] & (1 << 3))
		store_vesa_idx(prxcap, HDMIV_1024x768p60hz);
}

static void Edid_StandardTimingIII(struct rx_cap *prxcap, unsigned char *data)
{
	if (data[0] & (1 << 0))
		store_vesa_idx(prxcap, HDMIV_1152x864p75hz);
	if (data[1] & (1 << 6))
		store_vesa_idx(prxcap, HDMIV_1280x768p60hz);
	if (data[1] & (1 << 2))
		store_vesa_idx(prxcap, HDMIV_1280x960p60hz);
	if (data[1] & (1 << 1))
		store_vesa_idx(prxcap, HDMIV_1280x1024p60hz);
	if (data[2] & (1 << 7))
		store_vesa_idx(prxcap, HDMIV_1360x768p60hz);
	if (data[2] & (1 << 1))
		store_vesa_idx(prxcap, HDMIV_1400x1050p60hz);
	if (data[3] & (1 << 5))
		store_vesa_idx(prxcap, HDMIV_1680x1050p60hz);
	if (data[3] & (1 << 2))
		store_vesa_idx(prxcap, HDMIV_1600x1200p60hz);
	if (data[4] & (1 << 0))
		store_vesa_idx(prxcap, HDMIV_1920x1200p60hz);
}

static void calc_timing(unsigned char *data, struct vesa_standard_timing *t)
{
	struct hdmi_format_para *para = NULL;

	if ((data[0] < 2) && (data[1] < 2))
		return;
	t->hactive = (data[0] + 31) * 8;
	switch ((data[1] >> 6) & 0x3) {
	case 0:
		t->vactive = t->hactive * 5 / 8;
		break;
	case 1:
		t->vactive = t->hactive * 3 / 4;
		break;
	case 2:
		t->vactive = t->hactive * 4 / 5;
		break;
	case 3:
	default:
		t->vactive = t->hactive * 9 / 16;
		break;
	}
	t->hsync = (data[1] & 0x3f) + 60;
	para = hdmi_get_vesa_paras(t);
	if (para)
		t->vesa_timing = para->vic;

}

static void Edid_StandardTiming(struct rx_cap *prxcap, unsigned char *data,
				int max_num)
{
	int i;
	struct vesa_standard_timing timing;

	for (i = 0; i < max_num; i++) {
		memset(&timing, 0, sizeof(struct vesa_standard_timing));
		calc_timing(&data[i * 2], &timing);
		if (timing.vesa_timing)
			store_vesa_idx(prxcap, timing.vesa_timing);
	}
}

static void Edid_ReceiverProductNameParse(struct rx_cap *prxcap,
					  unsigned char *data)
{
	int i = 0;
	/* some Display Product name end with 0x20, not 0x0a
	 */
	while ((data[i] != 0x0a) && (data[i] != 0x20) && (i < 13)) {
		prxcap->ReceiverProductName[i] = data[i];
		i++;
	}
	prxcap->ReceiverProductName[i] = '\0';
}

void Edid_DecodeStandardTiming(struct hdmitx_info *info,
	unsigned char *Data, unsigned char length)
{
	 unsigned char  i, TmpVal;
	 int hor_pixel, frame_rate;

	for (i = 0; i < length; i++) {
		if ((Data[i*2] != 0x01) && (Data[i*2 + 1] != 0x01)) {
			hor_pixel = (int)((Data[i*2]+31)*8);
			TmpVal = Data[i*2 + 1] & 0xC0;

			frame_rate = (int)((Data[i*2 + 1]) & 0x3F) + 60;

			if ((hor_pixel == 720) && (frame_rate == 60))
				info->hdmi_sup_480p  = 1;
			else if ((hor_pixel == 1280) && (frame_rate == 60))
				info->hdmi_sup_720p_60hz  = 1;
			else if ((hor_pixel == 1920) && (frame_rate == 60))
				info->hdmi_sup_1080p_60hz  = 1;
		}
	}
}

/* ----------------------------------------------------------- */
void Edid_ParseCEADetailedTimingDescriptors(struct hdmitx_info *info,
	unsigned char blk_mun, unsigned char BaseAddr,
	unsigned char *buff)
{
	unsigned char index_edid;

	for (index_edid = 0; index_edid < blk_mun; index_edid++) {
		BaseAddr += 18;
		/* there is not the TimingDescriptors */
		if ((BaseAddr + 18) > 0x7d)
			break;
	}
}

static struct vsdb_phyaddr vsdb_local = {0};
int get_vsdb_phy_addr(struct vsdb_phyaddr *vsdb)
{
	vsdb = &vsdb_local;
	return vsdb->valid;
}

static void set_vsdb_phy_addr(struct hdmitx_dev *hdev,
			      struct vsdb_phyaddr *vsdb,
			      unsigned char *edid_offset)
{
	int phy_addr;

	vsdb->a = (edid_offset[4] >> 4) & 0xf;
	vsdb->b = (edid_offset[4] >> 0) & 0xf;
	vsdb->c = (edid_offset[5] >> 4) & 0xf;
	vsdb->d = (edid_offset[5] >> 0) & 0xf;
	vsdb_local = *vsdb;
	vsdb->valid = 1;

	phy_addr = ((vsdb->a & 0xf) << 12) |
		   ((vsdb->b & 0xf) <<  8) |
		   ((vsdb->c & 0xf) <<  4) |
		   ((vsdb->d & 0xf) <<  0);
	hdev->physical_addr = phy_addr;
	hdmitx_event_notify(HDMITX_PHY_ADDR_VALID, &phy_addr);
}

static void set_vsdb_dc_cap(struct rx_cap *prxcap)
{
	prxcap->dc_y444 = !!(prxcap->ColorDeepSupport & (1 << 3));
	prxcap->dc_30bit = !!(prxcap->ColorDeepSupport & (1 << 4));
	prxcap->dc_36bit = !!(prxcap->ColorDeepSupport & (1 << 5));
	prxcap->dc_48bit = !!(prxcap->ColorDeepSupport & (1 << 6));
}

static void set_vsdb_dc_420_cap(struct rx_cap *prxcap,
				unsigned char *edid_offset)
{
	prxcap->dc_30bit_420 = !!(edid_offset[6] & (1 << 0));
	prxcap->dc_36bit_420 = !!(edid_offset[6] & (1 << 1));
	prxcap->dc_48bit_420 = !!(edid_offset[6] & (1 << 2));
}

/* Special FBC check */
static int check_fbc_special(unsigned char *edid_dat)
{
	if ((edid_dat[250] == 0xfb) && (edid_dat[251] == 0x0c))
		return 1;
	else
		return 0;
}

int Edid_Parse_check_HDMI_VSDB(struct hdmitx_dev *hdev,
	unsigned char *buff)
{
	int ret = 0;
	struct hdmitx_info *info = &(hdev->hdmi_info);
	unsigned char  VSpecificBoundary, BlockAddr, len;
	int temp_addr = 0;

	VSpecificBoundary = buff[2];

	if (VSpecificBoundary < 4)
		ret = -1;
	BlockAddr = CEA_DATA_BLOCK_COLLECTION_ADDR_1StP;
	while (BlockAddr < VSpecificBoundary) {
		len = buff[BlockAddr] & 0x1F;
		if ((buff[BlockAddr] & 0xE0) == VENDOR_TAG)
			break;
		temp_addr = BlockAddr + len + 1;
		if (temp_addr >= VSpecificBoundary)
			break;
		BlockAddr = BlockAddr + len + 1;
	}

	set_vsdb_phy_addr(hdev, &info->vsdb_phy_addr, &buff[BlockAddr]);
	if ((check_fbc_special(&hdev->EDID_buf[0])) ||
	    (check_fbc_special(&hdev->EDID_buf1[0])))
		rx_edid_physical_addr(0, 0, 0, 0);
	else
		rx_edid_physical_addr(info->vsdb_phy_addr.a,
			info->vsdb_phy_addr.b,
			info->vsdb_phy_addr.c,
			info->vsdb_phy_addr.d);

	if (temp_addr >= VSpecificBoundary)
		ret = -1;
	else {
		if ((buff[BlockAddr + 1] != GET_OUI_BYTE0(HDMI_IEEEOUI)) ||
			(buff[BlockAddr + 2] != GET_OUI_BYTE1(HDMI_IEEEOUI)) ||
			(buff[BlockAddr + 3] != GET_OUI_BYTE2(HDMI_IEEEOUI)))
			ret = -1;
	}
	return ret;
}

/* ----------------------------------------------------------- */
void Edid_MonitorCapable861(struct hdmitx_info *info,
	unsigned char edid_flag)
{
	if (edid_flag & 0x80)
		info->support_underscan_flag = 1;
	if (edid_flag & 0x40) {
		struct hdmitx_dev *hdev =
			container_of(info, struct hdmitx_dev, hdmi_info);
		info->support_basic_audio_flag = 1;
		hdmitx_edid_set_default_aud(hdev);
	}
	if (edid_flag & 0x20)
		info->support_ycbcr444_flag = 1;
	if (edid_flag & 0x10)
		info->support_ycbcr422_flag = 1;
}


/* ----------------------------------------------------------- */
static void Edid_ParsingVideoDATABlock(struct hdmitx_info *info,
	unsigned char *buff, unsigned char BaseAddr,
	unsigned char NBytes)
{
	unsigned char i;

	NBytes &= 0x1F;
	for (i = 0; i < NBytes; i++) {
		switch (buff[i + BaseAddr]&0x7F) {
		case 6:
		case 7:
			info->hdmi_sup_480i  = 1;
			break;
		case 21:
		case 22:
			info->hdmi_sup_576i  = 1;
			break;
		case 2:
		case 3:
			info->hdmi_sup_480p  = 1;
			break;
		case 17:
		case 18:
			info->hdmi_sup_576p  = 1;
			break;
		case 4:
			info->hdmi_sup_720p_60hz  = 1;
			break;
		case 19:
			info->hdmi_sup_720p_50hz  = 1;
			break;
		case 5:
			info->hdmi_sup_1080i_60hz  = 1;
			break;
		case 20:
			info->hdmi_sup_1080i_50hz  = 1;
			break;
		case 16:
			info->hdmi_sup_1080p_60hz  = 1;
			break;
		case 31:
			info->hdmi_sup_1080p_50hz  = 1;
			break;
		case 32:
			info->hdmi_sup_1080p_24hz  = 1;
			break;
		case 33:
			info->hdmi_sup_1080p_25hz  = 1;
			break;
		case 34:
			info->hdmi_sup_1080p_30hz  = 1;
			break;
		default:
			break;
		}
	}
}

/* ----------------------------------------------------------- */
static void Edid_ParsingAudioDATABlock(struct hdmitx_info *info,
	unsigned char *Data, unsigned char BaseAddr,
	unsigned char NBytes)
{
	 unsigned char AudioFormatCode;
	 int i = BaseAddr;
	NBytes &= 0x1F;
	do {
		AudioFormatCode = (Data[i]&0xF8)>>3;
		switch (AudioFormatCode) {
		case 1:
			info->tv_audio_info._60958_PCM.support_flag = 1;
			info->tv_audio_info._60958_PCM.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._60958_PCM._192k =
					1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._60958_PCM._176k =
					1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._60958_PCM._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._60958_PCM._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._60958_PCM._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._60958_PCM._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._60958_PCM._32k = 1;
			if ((Data[i+2]&0x04))
				info->tv_audio_info._60958_PCM._24bit = 1;
			if ((Data[i+2]&0x02))
				info->tv_audio_info._60958_PCM._20bit = 1;
			if ((Data[i+2]&0x01))
				info->tv_audio_info._60958_PCM._16bit = 1;
			break;
		case 2:
			info->tv_audio_info._AC3.support_flag = 1;
			info->tv_audio_info._AC3.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._AC3._192k = 1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._AC3._176k = 1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._AC3._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._AC3._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._AC3._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._AC3._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._AC3._32k = 1;
			info->tv_audio_info._AC3._max_bit =
				Data[i+2];
			break;
		case 3:
			info->tv_audio_info._MPEG1.support_flag = 1;
			info->tv_audio_info._MPEG1.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._MPEG1._192k = 1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._MPEG1._176k = 1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._MPEG1._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._MPEG1._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._MPEG1._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._MPEG1._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._MPEG1._32k = 1;
			info->tv_audio_info._MPEG1._max_bit =
				Data[i+2];
			break;
		case 4:
			info->tv_audio_info._MP3.support_flag = 1;
			info->tv_audio_info._MP3.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._MP3._192k = 1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._MP3._176k = 1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._MP3._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._MP3._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._MP3._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._MP3._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._MP3._32k = 1;
			info->tv_audio_info._MP3._max_bit = Data[i+2];
			break;
		case 5:
			info->tv_audio_info._MPEG2.support_flag = 1;
			info->tv_audio_info._MPEG2.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._MPEG2._192k = 1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._MPEG2._176k = 1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._MPEG2._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._MPEG2._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._MPEG2._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._MPEG2._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._MPEG2._32k = 1;
			info->tv_audio_info._MPEG2._max_bit = Data[i+2];
			break;
		case 6:
			info->tv_audio_info._AAC.support_flag = 1;
			info->tv_audio_info._AAC.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._AAC._192k = 1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._AAC._176k = 1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._AAC._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._AAC._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._AAC._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._AAC._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._AAC._32k = 1;
			info->tv_audio_info._AAC._max_bit = Data[i+2];
			break;
		case 7:
			info->tv_audio_info._DTS.support_flag = 1;
			info->tv_audio_info._DTS.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._DTS._192k = 1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._DTS._176k = 1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._DTS._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._DTS._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._DTS._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._DTS._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._DTS._32k = 1;
			info->tv_audio_info._DTS._max_bit = Data[i+2];
			break;
		case 8:
			info->tv_audio_info._ATRAC.support_flag = 1;
			info->tv_audio_info._ATRAC.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._ATRAC._192k = 1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._ATRAC._176k = 1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._ATRAC._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._ATRAC._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._ATRAC._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._ATRAC._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._ATRAC._32k = 1;
			info->tv_audio_info._ATRAC._max_bit = Data[i+2];
			break;
		case 9:
			info->tv_audio_info._One_Bit_Audio.support_flag = 1;
			info->tv_audio_info._One_Bit_Audio.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._One_Bit_Audio._192k = 1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._One_Bit_Audio._176k = 1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._One_Bit_Audio._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._One_Bit_Audio._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._One_Bit_Audio._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._One_Bit_Audio._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._One_Bit_Audio._32k = 1;
			info->tv_audio_info._One_Bit_Audio._max_bit =
				Data[i+2];
			break;
		case 10:
			info->tv_audio_info._Dolby.support_flag = 1;
			info->tv_audio_info._Dolby.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._Dolby._192k = 1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._Dolby._176k = 1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._Dolby._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._Dolby._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._Dolby._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._Dolby._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._Dolby._32k = 1;
			info->tv_audio_info._Dolby._max_bit = Data[i+2];
			break;

		case 11:
			info->tv_audio_info._DTS_HD.support_flag = 1;
			info->tv_audio_info._DTS_HD.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._DTS_HD._192k = 1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._DTS_HD._176k = 1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._DTS_HD._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._DTS_HD._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._DTS_HD._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._DTS_HD._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._DTS_HD._32k = 1;
			info->tv_audio_info._DTS_HD._max_bit =
				Data[i+2];
			break;
		case 12:
			info->tv_audio_info._MAT.support_flag = 1;
			info->tv_audio_info._MAT.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._MAT._192k = 1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._MAT._176k = 1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._MAT._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._MAT._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._MAT._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._MAT._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._MAT._32k = 1;
			info->tv_audio_info._MAT._max_bit = Data[i+2];
			break;

		case 13:
			info->tv_audio_info._ATRAC.support_flag = 1;
			info->tv_audio_info._ATRAC.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._DST._192k = 1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._DST._176k = 1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._DST._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._DST._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._DST._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._DST._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._DST._32k = 1;
			info->tv_audio_info._DST._max_bit = Data[i+2];
			break;

		case 14:
			info->tv_audio_info._WMA.support_flag = 1;
			info->tv_audio_info._WMA.max_channel_num =
				(Data[i]&0x07);
			if ((Data[i+1]&0x40))
				info->tv_audio_info._WMA._192k = 1;
			if ((Data[i+1]&0x20))
				info->tv_audio_info._WMA._176k = 1;
			if ((Data[i+1]&0x10))
				info->tv_audio_info._WMA._96k = 1;
			if ((Data[i+1]&0x08))
				info->tv_audio_info._WMA._88k = 1;
			if ((Data[i+1]&0x04))
				info->tv_audio_info._WMA._48k = 1;
			if ((Data[i+1]&0x02))
				info->tv_audio_info._WMA._44k = 1;
			if ((Data[i+1]&0x01))
				info->tv_audio_info._WMA._32k = 1;
			info->tv_audio_info._WMA._max_bit = Data[i+2];
			break;

		default:
		break;
		}
	i += 3;
	} while (i < (NBytes + BaseAddr));
}

/* ----------------------------------------------------------- */
static void Edid_ParsingSpeakerDATABlock(struct hdmitx_info *info,
	unsigned char *buff, unsigned char BaseAddr)
{
	int ii;

	for (ii = 1; ii < 0x80; ) {
		switch (buff[BaseAddr] & ii) {
		case 0x40:
			info->tv_audio_info.speaker_allocation.rlc_rrc = 1;
			break;

		case 0x20:
			info->tv_audio_info.speaker_allocation.flc_frc = 1;
			break;

		case 0x10:
			info->tv_audio_info.speaker_allocation.rc = 1;
			break;

		case 0x08:
			info->tv_audio_info.speaker_allocation.rl_rr = 1;
			break;

		case 0x04:
			info->tv_audio_info.speaker_allocation.fc = 1;
			break;

		case 0x02:
			info->tv_audio_info.speaker_allocation.lfe = 1;
			break;

		case 0x01:
			info->tv_audio_info.speaker_allocation.fl_fr = 1;
			break;

		default:
		break;
		}
		ii = ii << 1;
	}
}

static void _Edid_ParsingVendSpec(struct dv_info *dv,
				  struct hdr10_plus_info *hdr10_plus,
				  unsigned char *buf)
{
	unsigned char *dat = buf;
	unsigned char pos = 0;
	unsigned int ieeeoui = 0;
	u8 length = 0;

	length = dat[pos] & 0x1f;
	pos++;

	if (dat[pos] != 1) {
		pr_info("hdmitx: edid: parsing fail %s[%d]\n", __func__,
			__LINE__);
		return;
	}

	pos++;
	ieeeoui = dat[pos++];
	ieeeoui += dat[pos++] << 8;
	ieeeoui += dat[pos++] << 16;
	pr_info("Edid_ParsingVendSpec:ieeeoui=0x%x,len=%u\n", ieeeoui, length);

/*HDR10+ use vsvdb*/
	if (ieeeoui == HDR10_PLUS_IEEE_OUI) {
		memset(hdr10_plus, 0, sizeof(struct hdr10_plus_info));
		hdr10_plus->length = length;
		hdr10_plus->ieeeoui = ieeeoui;
		hdr10_plus->application_version = dat[pos] & 0x3;
		pos++;
		return;
	}

	if (ieeeoui != DV_IEEE_OUI) {
		dv->block_flag = ERROR_OUI;
		return;
	}

/* it is a Dovi block*/
	memset(dv, 0, sizeof(struct dv_info));
	dv->block_flag = CORRECT;
	dv->length = length;
	memcpy(dv->rawdata, dat, dv->length + 1);
	dv->ieeeoui = ieeeoui;
	dv->ver = (dat[pos] >> 5) & 0x7;
	if ((dv->ver) > 2) {
		dv->block_flag = ERROR_VER;
		return;
	}
	/* Refer to DV 2.9 Page 27 */
	if (dv->ver == 0) {
		if (dv->length == 0x19) {
			dv->sup_yuv422_12bit = dat[pos] & 0x1;
			dv->sup_2160p60hz = (dat[pos] >> 1) & 0x1;
			dv->sup_global_dimming = (dat[pos] >> 2) & 0x1;
			pos++;
			dv->Rx =
				(dat[pos+1] << 4) | (dat[pos] >> 4);
			dv->Ry =
				(dat[pos+2] << 4) | (dat[pos] & 0xf);
			pos += 3;
			dv->Gx =
				(dat[pos+1] << 4) | (dat[pos] >> 4);
			dv->Gy =
				(dat[pos+2] << 4) | (dat[pos] & 0xf);
			pos += 3;
			dv->Bx =
				(dat[pos+1] << 4) | (dat[pos] >> 4);
			dv->By =
				(dat[pos+2] << 4) | (dat[pos] & 0xf);
			pos += 3;
			dv->Wx =
				(dat[pos+1] << 4) | (dat[pos] >> 4);
			dv->Wy =
				(dat[pos+2] << 4) | (dat[pos] & 0xf);
			pos += 3;
			dv->tminPQ =
				(dat[pos+1] << 4) | (dat[pos] >> 4);
			dv->tmaxPQ =
				(dat[pos+2] << 4) | (dat[pos] & 0xf);
			pos += 3;
			dv->dm_major_ver = dat[pos] >> 4;
			dv->dm_minor_ver = dat[pos] & 0xf;
			pos++;
			pr_info("v0 VSVDB: len=%d, sup_2160p60hz=%d\n",
				dv->length, dv->sup_2160p60hz);
		} else
			dv->block_flag = ERROR_LENGTH;
	}

	if (dv->ver == 1) {
		if (dv->length == 0x0B) {/* Refer to DV 2.9 Page 33 */
			dv->dm_version = (dat[pos] >> 2) & 0x7;
			dv->sup_yuv422_12bit = dat[pos] & 0x1;
			dv->sup_2160p60hz = (dat[pos] >> 1) & 0x1;
			pos++;
			dv->sup_global_dimming = dat[pos] & 0x1;
			dv->tmaxLUM = dat[pos] >> 1;
			pos++;
			dv->colorimetry = dat[pos] & 0x1;
			dv->tminLUM = dat[pos] >> 1;
			pos++;
			dv->low_latency = dat[pos] & 0x3;
			dv->Bx = 0x20 | ((dat[pos] >> 5) & 0x7);
			dv->By = 0x08 | ((dat[pos] >> 2) & 0x7);
			pos++;
			dv->Gx = 0x00 | (dat[pos] >> 1);
			dv->Ry = 0x40 | ((dat[pos] & 0x1) |
				((dat[pos + 1] & 0x1) << 1) |
				((dat[pos + 2] & 0x3) << 2));
			pos++;
			dv->Gy = 0x80 | (dat[pos] >> 1);
			pos++;
			dv->Rx = 0xA0 | (dat[pos] >> 3);
			pos++;
			pr_info("v1 VSVDB: len=%d, sup_2160p60hz=%d, low_latency=%d\n",
				dv->length, dv->sup_2160p60hz, dv->low_latency);
		} else if (dv->length == 0x0E) {
			dv->dm_version = (dat[pos] >> 2) & 0x7;
			dv->sup_yuv422_12bit = dat[pos] & 0x1;
			dv->sup_2160p60hz = (dat[pos] >> 1) & 0x1;
			pos++;
			dv->sup_global_dimming = dat[pos] & 0x1;
			dv->tmaxLUM = dat[pos] >> 1;
			pos++;
			dv->colorimetry = dat[pos] & 0x1;
			dv->tminLUM = dat[pos] >> 1;
			pos += 2; /* byte8 is reserved as 0 */
			dv->Rx = dat[pos++];
			dv->Ry = dat[pos++];
			dv->Gx = dat[pos++];
			dv->Gy = dat[pos++];
			dv->Bx = dat[pos++];
			dv->By = dat[pos++];
			pr_info("v1 VSVDB: len=%d, sup_2160p60hz=%d\n",
				dv->length, dv->sup_2160p60hz);
		} else
			dv->block_flag = ERROR_LENGTH;
	}
	if (dv->ver == 2) {
		/* v2 VSVDB length could be greater than 0xB
		 * and should not be treated as unrecognized
		 * block. Instead, we should parse it as a regular
		 * v2 VSVDB using just the remaining 11 bytes here
		 */
		if (dv->length >= 0x0B) {
			dv->sup_2160p60hz = 0x1;/*default*/
			dv->dm_version = (dat[pos] >> 2) & 0x7;
			dv->sup_yuv422_12bit = dat[pos] & 0x1;
			dv->sup_backlight_control = (dat[pos] >> 1) & 0x1;
			pos++;
			dv->sup_global_dimming = (dat[pos] >> 2) & 0x1;
			dv->backlt_min_luma = dat[pos] & 0x3;
			dv->tminPQ = dat[pos] >> 3;
			pos++;
			dv->Interface = dat[pos] & 0x3;
			dv->tmaxPQ = dat[pos] >> 3;
			pos++;
			dv->sup_10b_12b_444 = ((dat[pos] & 0x1) << 1) |
				(dat[pos + 1] & 0x1);
			dv->Gx = 0x00 | (dat[pos] >> 1);
			pos++;
			dv->Gy = 0x80 | (dat[pos] >> 1);
			pos++;
			dv->Rx = 0xA0 | (dat[pos] >> 3);
			dv->Bx = 0x20 | (dat[pos] & 0x7);
			pos++;
			dv->Ry = 0x40  | (dat[pos] >> 3);
			dv->By = 0x08  | (dat[pos] & 0x7);
			pos++;
			pr_info("v2 VSVDB: len=%d, sup_2160p60hz=%d, Interface=%d\n",
				dv->length, dv->sup_2160p60hz, dv->Interface);
		} else
			dv->block_flag = ERROR_LENGTH;
	}

	if (pos > (dv->length + 1))
		pr_info("hdmitx: edid: maybe invalid dv%d data\n", dv->ver);
}

static void Edid_ParsingVendSpec(struct hdmitx_dev *hdev,
				 struct rx_cap *prxcap,
				 unsigned char *buf)
{
	struct dv_info *dv = &prxcap->dv_info;
	struct dv_info *dv2 = &prxcap->dv_info2;
	struct hdr10_plus_info *hdr10_plus = &prxcap->hdr10plus_info;

	if (hdev->hdr_priority) { /* skip dv_info parsing */
		_Edid_ParsingVendSpec(dv2, hdr10_plus, buf);
		return;
	}
	_Edid_ParsingVendSpec(dv, hdr10_plus, buf);
	_Edid_ParsingVendSpec(dv2, hdr10_plus, buf);
}

/* ----------------------------------------------------------- */
static int Edid_ParsingY420VDBBlock(struct rx_cap *prxcap,
				    unsigned char *buf)
{
	unsigned char tag = 0, ext_tag = 0, data_end = 0;
	unsigned int pos = 0;
	int i = 0, found = 0;

	tag = (buf[pos] >> 5) & 0x7;
	data_end = (buf[pos] & 0x1f)+1;
	pos++;
	ext_tag = buf[pos];

	if ((tag != 0x7) || (ext_tag != 0xe))
		goto INVALID_Y420VDB;

	pos++;
	while (pos < data_end) {
		if (prxcap->VIC_count < VIC_MAX_NUM) {
			for (i = 0; i < prxcap->VIC_count; i++) {
				if (prxcap->VIC[i] == buf[pos]) {
					prxcap->VIC[i] =
					HDMITX_VIC420_OFFSET + buf[pos];
					found = 1;
					/* Here we do not break,because
					 *	some EDID may have the same
					 *	repeated VICs
					 */
				}
			}
			if (found == 0) {
				prxcap->VIC[prxcap->VIC_count] =
				HDMITX_VIC420_OFFSET + buf[pos];
				prxcap->VIC_count++;
			}
		}
		pos++;
	}

	return 0;

INVALID_Y420VDB:
	pr_info("[%s] it's not a valid y420vdb!\n", __func__);
	return -1;
}

static int Edid_ParsingDRMStaticBlock(struct rx_cap *prxcap,
				      unsigned char *buf)
{
	unsigned char tag = 0, ext_tag = 0, data_end = 0;
	unsigned int pos = 0;

	tag = (buf[pos] >> 5) & 0x7;
	data_end = (buf[pos] & 0x1f);
	memset(prxcap->hdr_rawdata, 0, 7);
	memcpy(prxcap->hdr_rawdata, buf, data_end + 1);
	pos++;
	ext_tag = buf[pos];
	if ((tag != HDMI_EDID_BLOCK_TYPE_EXTENDED_TAG)
		|| (ext_tag != EXTENSION_DRM_STATIC_TAG))
		goto INVALID_DRM_STATIC;
	pos++;
	prxcap->hdr_sup_eotf_sdr = !!(buf[pos] & (0x1 << 0));
	prxcap->hdr_sup_eotf_hdr = !!(buf[pos] & (0x1 << 1));
	prxcap->hdr_sup_eotf_smpte_st_2084 = !!(buf[pos] & (0x1 << 2));
	prxcap->hdr_sup_eotf_hlg = !!(buf[pos] & (0x1 << 3));
	pos++;
	prxcap->hdr_sup_SMD_type1 = !!(buf[pos] & (0x1 << 0));
	pos++;
	if (data_end == 3)
		return 0;
	if (data_end == 4) {
		prxcap->hdr_lum_max = buf[pos];
		return 0;
	}
	if (data_end == 5) {
		prxcap->hdr_lum_max = buf[pos];
		prxcap->hdr_lum_avg = buf[pos + 1];
		return 0;
	}
	if (data_end == 6) {
		prxcap->hdr_lum_max = buf[pos];
		prxcap->hdr_lum_avg = buf[pos + 1];
		prxcap->hdr_lum_min = buf[pos + 2];
		return 0;
	}
	return 0;
INVALID_DRM_STATIC:
	pr_info("[%s] it's not a valid DRM STATIC BLOCK\n", __func__);
	return -1;
}

static int Edid_ParsingDRMDynamicBlock(struct rx_cap *prxcap,
				       unsigned char *buf)
{
	unsigned char tag = 0, ext_tag = 0, data_end = 0;
	unsigned int pos = 0;
	unsigned int type;
	unsigned int type_length;
	unsigned int i;
	unsigned int num;

	tag = (buf[pos] >> 5) & 0x7;
	data_end = (buf[pos] & 0x1f);
	pos++;
	ext_tag = buf[pos];
	if ((tag != HDMI_EDID_BLOCK_TYPE_EXTENDED_TAG)
		|| (ext_tag != EXTENSION_DRM_DYNAMIC_TAG))
		goto INVALID_DRM_DYNAMIC;
	pos++;
	data_end--;/*extended tag code byte doesn't need*/

	while (data_end) {
		type_length = buf[pos];
		pos++;
		type = (buf[pos + 1] << 8) | buf[pos];
		pos += 2;
		switch (type) {
		case TS_103_433_SPEC_TYPE:
			num = 1;
			break;
		case ITU_T_H265_SPEC_TYPE:
			num = 2;
			break;
		case TYPE_4_HDR_METADATA_TYPE:
			num = 3;
			break;
		case TYPE_1_HDR_METADATA_TYPE:
		default:
			num = 0;
			break;
		}
		prxcap->hdr_dynamic_info[num].hd_len = type_length;
		prxcap->hdr_dynamic_info[num].type = type;
		prxcap->hdr_dynamic_info[num].support_flags = buf[pos];
		pos++;
		for (i = 0; i < type_length - 3; i++) {
			prxcap->hdr_dynamic_info[num].optional_fields[i]
				= buf[pos];
			pos++;
		}
		data_end = data_end - (type_length + 1);
	}

	return 0;
INVALID_DRM_DYNAMIC:
	pr_info("[%s] it's not a valid DRM DYNAMIC BLOCK\n", __func__);
	return -1;
}

static int Edid_ParsingVFPDB(struct rx_cap *prxcap, unsigned char *buf)
{
	unsigned int len = buf[0] & 0x1f;
	enum hdmi_vic svr = HDMI_Unknown;

	if (buf[1] != EXTENSION_VFPDB_TAG)
		return 0;
	if (len < 2)
		return 0;

	svr = buf[2];
	if (((svr >= 1) && (svr <= 127)) ||
		((svr >= 193) && (svr <= 253))) {
		prxcap->flag_vfpdb = 1;
		prxcap->preferred_mode = svr;
		pr_info("preferred mode 0 srv %d\n", prxcap->preferred_mode);
		return 1;
	}
	if ((svr >= 129) && (svr <= 144)) {
		prxcap->flag_vfpdb = 1;
		prxcap->preferred_mode = prxcap->dtd[svr - 129].vic;
		pr_info("preferred mode 0 dtd %d\n", prxcap->preferred_mode);
		return 1;
	}
	return 0;
}

/* ----------------------------------------------------------- */
static int Edid_ParsingY420CMDBBlock(struct hdmitx_info *info,
	unsigned char *buf)
{
	unsigned char tag = 0, ext_tag = 0, length = 0, data_end = 0;
	unsigned int pos = 0, i = 0;

	tag = (buf[pos] >> 5) & 0x7;
	length = buf[pos] & 0x1f;
	data_end = length + 1;
	pos++;
	ext_tag = buf[pos];

	if ((tag != 0x7) || (ext_tag != 0xf))
		goto INVALID_Y420CMDB;

	if (length == 1) {
		info->y420_all_vic = 1;
		return 0;
	}

	info->bitmap_length = 0;
	info->bitmap_valid = 0;
	memset(info->y420cmdb_bitmap, 0x00, Y420CMDB_MAX);

	pos++;
	if (pos < data_end) {
		info->bitmap_length = data_end - pos;
		info->bitmap_valid = 1;
	}
	while (pos < data_end) {
		if (i < Y420CMDB_MAX)
			info->y420cmdb_bitmap[i] = buf[pos];
		pos++;
		i++;
	}

	return 0;

INVALID_Y420CMDB:
	pr_info("[%s] it's not a valid y420cmdb!\n", __func__);
	return -1;

}

static int Edid_Y420CMDB_fill_all_vic(struct hdmitx_dev *hdmitx_device)
{
	struct rx_cap *rxcap = &hdmitx_device->rxcap;
	struct hdmitx_info *info = &hdmitx_device->hdmi_info;
	unsigned int count = rxcap->VIC_count;
	unsigned int a, b;

	if (info->y420_all_vic != 1)
		return 1;

	a = count/8;
	a = (a >= Y420CMDB_MAX)?Y420CMDB_MAX:a;
	b = count%8;

	if (a > 0)
		memset(&(info->y420cmdb_bitmap[0]), 0xff, a);

	if ((b != 0) && (a < Y420CMDB_MAX))
		info->y420cmdb_bitmap[a] = (1 << b) - 1;

	info->bitmap_length = (b == 0) ? a : (a + 1);
	info->bitmap_valid = (info->bitmap_length != 0)?1:0;

	return 0;
}

static int Edid_Y420CMDB_PostProcess(struct hdmitx_dev *hdmitx_device)
{
	unsigned int i = 0, j = 0, valid = 0;
	struct rx_cap *rxcap = &hdmitx_device->rxcap;
	struct hdmitx_info *info = &hdmitx_device->hdmi_info;
	unsigned char *p = NULL;

	if (info->y420_all_vic == 1)
		Edid_Y420CMDB_fill_all_vic(hdmitx_device);

	if (info->bitmap_valid == 0)
		goto PROCESS_END;

	for (i = 0; i < info->bitmap_length; i++) {
		p = &(info->y420cmdb_bitmap[i]);
		for (j = 0; j < 8; j++) {
			valid = ((*p >> j) & 0x1);
			if (valid != 0) {
				rxcap->VIC[rxcap->VIC_count] =
				HDMITX_VIC420_OFFSET + rxcap->VIC[i*8+j];
				rxcap->VIC_count++;
			}
		}
	}

PROCESS_END:
	return 0;
}

static void Edid_Y420CMDB_Reset(struct hdmitx_info *info)
{
	info->bitmap_valid = 0;
	info->bitmap_length = 0;
	info->y420_all_vic = 0;
	memset(info->y420cmdb_bitmap, 0x00, Y420CMDB_MAX);
}

/* ----------------------------------------------------------- */
int Edid_ParsingCEADataBlockCollection(struct hdmitx_dev *hdmitx_device,
	unsigned char *buff)
{
	unsigned char AddrTag, D, Addr, Data;
	int temp_addr;
	int len;
	struct hdmitx_info *info = &(hdmitx_device->hdmi_info);
	struct rx_cap *prxcap = &hdmitx_device->rxcap;

	/* Byte number offset d where Detailed Timing data begins */
	D = buff[2];
	Addr = 4;

	AddrTag = Addr;
	do {
		Data = buff[AddrTag];
		switch (Data&0xE0) {
		case VIDEO_TAG:
			if ((Addr + (Data&0x1f)) < D) {
				Edid_ParsingVideoDATABlock(info, buff,
					Addr + 1, (Data & 0x1F));
				len = (Data & 0x1f) + 1;
				if ((prxcap->vsd.len + len) > MAX_RAW_LEN)
					break;
				memcpy(&prxcap->vsd.raw[prxcap->vsd.len],
				       &buff[AddrTag], len);
				prxcap->vsd.len += len;
			}
			break;

		case AUDIO_TAG:
			/* rx_set_receiver_edid(&buff[AddrTag], len); */
			if ((Addr + (Data&0x1f)) < D)
				Edid_ParsingAudioDATABlock(info, buff,
					Addr + 1, (Data & 0x1F));
			len = (Data & 0x1f) + 1;
			if ((prxcap->asd.len + len) > MAX_RAW_LEN)
				break;
			memcpy(&prxcap->asd.raw[prxcap->asd.len],
			       &buff[AddrTag], len);
			prxcap->asd.len += len;
			break;

		case SPEAKER_TAG:
			if ((Addr + (Data&0x1f)) < D)
				Edid_ParsingSpeakerDATABlock(info, buff,
					Addr + 1);
			break;

		case VENDOR_TAG:
			if ((Addr + (Data&0x1f)) < D) {
				if ((buff[Addr + 1] != 0x03) ||
					(buff[Addr + 2] != 0x0c) ||
					(buff[Addr + 3] != 0x00)) {
					info->auth_state = HDCP_NO_AUTH;
				}
				if ((Data&0x1f) > 5) {
					if (buff[Addr + 6] & 0x80)
						info->support_ai_flag = 1;
				}
			}
			break;

		default:
		break;
		}
		Addr += (Data & 0x1F);   /* next Tag Address */
		AddrTag = ++Addr;
		Data = buff[Addr];
		temp_addr =   Addr + (Data & 0x1F);
		if (temp_addr >= D)	/* force to break; */
			break;
	} while (Addr < D);

	return 0;
}

/* ----------------------------------------------------------- */

/* parse Sink 3D information */
static int hdmitx_edid_3d_parse(struct rx_cap *prxcap, unsigned char *dat,
				unsigned int size)
{
	int j = 0;
	unsigned int base = 0;
	unsigned int pos = base + 1;

	if (dat[base] & (1 << 7))
		pos += 2;
	if (dat[base] & (1 << 6))
		pos += 2;
	if (dat[base] & (1 << 5)) {
		prxcap->threeD_present = dat[pos] >> 7;
		prxcap->threeD_Multi_present = (dat[pos] >> 5) & 0x3;
		pos += 1;
		prxcap->hdmi_vic_LEN = dat[pos] >> 5;
		prxcap->HDMI_3D_LEN = dat[pos] & 0x1f;
		pos += prxcap->hdmi_vic_LEN + 1;

		if (prxcap->threeD_Multi_present == 0x01) {
			prxcap->threeD_Structure_ALL_15_0 =
				(dat[pos] << 8) + dat[pos+1];
			prxcap->threeD_MASK_15_0 = 0;
			pos += 2;
		}
		if (prxcap->threeD_Multi_present == 0x02) {
			prxcap->threeD_Structure_ALL_15_0 =
				(dat[pos] << 8) + dat[pos+1];
			pos += 2;
			prxcap->threeD_MASK_15_0 =
				(dat[pos] << 8) + dat[pos + 1];
			pos += 2;
		}
	}
	while (pos < size) {
		if ((dat[pos] & 0xf) < 0x8) {
			/* frame packing */
			if ((dat[pos] & 0xf) == T3D_FRAME_PACKING)
				prxcap->support_3d_format[prxcap->VIC[((dat[pos]
					& 0xf0) >> 4)]].frame_packing = 1;
			/* top and bottom */
			if ((dat[pos] & 0xf) == T3D_TAB)
				prxcap->support_3d_format[prxcap->VIC[((dat[pos]
					& 0xf0) >> 4)]].top_and_bottom = 1;
			pos += 1;
		} else {
			/* SidebySide */
			if ((dat[pos] & 0xf) == T3D_SBS_HALF)
				if ((dat[pos+1] >> 4) < 0xb)
					prxcap->support_3d_format[prxcap->VIC[
						((dat[pos] & 0xf0) >> 4)]]
						.side_by_side = 1;
			pos += 2;
		}
	}
	if (prxcap->threeD_MASK_15_0 == 0) {
		for (j = 0; (j < 16) && (j < prxcap->VIC_count); j++) {
			prxcap->support_3d_format[prxcap->VIC[j]].frame_packing
				= 1;
			prxcap->support_3d_format[prxcap->VIC[j]].top_and_bottom
				= 1;
			prxcap->support_3d_format[prxcap->VIC[j]].side_by_side
				= 1;
		}
	} else {
		for (j = 0; j < 16; j++) {
			if (((prxcap->threeD_MASK_15_0) >> j) & 0x1) {
				/* frame packing */
				if (prxcap->threeD_Structure_ALL_15_0
					& (1 << 0))
					prxcap->support_3d_format[prxcap->
						VIC[j]].frame_packing = 1;
				/* top and bottom */
				if (prxcap->threeD_Structure_ALL_15_0
					& (1 << 6))
					prxcap->support_3d_format[prxcap->
						VIC[j]].top_and_bottom = 1;
				/* top and bottom */
				if (prxcap->threeD_Structure_ALL_15_0
					& (1 << 8))
					prxcap->support_3d_format[prxcap->
						VIC[j]].side_by_side = 1;
			}
		}
	}
	return 1;
}

/* parse Sink 4k2k information */
static void hdmitx_edid_4k2k_parse(struct rx_cap *prxcap, unsigned char *dat,
				   unsigned int size)
{
	if ((size > 4) || (size == 0)) {
		pr_info(EDID
			"4k2k in edid out of range, SIZE = %d\n",
			size);
		return;
	}
	while (size--) {
		if (*dat == 1)
			prxcap->VIC[prxcap->VIC_count] = HDMI_4k2k_30;
		else if (*dat == 2)
			prxcap->VIC[prxcap->VIC_count] = HDMI_4k2k_25;
		else if (*dat == 3)
			prxcap->VIC[prxcap->VIC_count] = HDMI_4k2k_24;
		else if (*dat == 4)
			prxcap->VIC[prxcap->VIC_count] = HDMI_4k2k_smpte_24;
		else
			;
		dat++;
		prxcap->VIC_count++;
	}
}

static void get_latency(struct rx_cap *prxcap, unsigned char *val)
{
	if (val[0] == 0)
		prxcap->vLatency = LATENCY_INVALID_UNKNOWN;
	else if (val[0] == 0xFF)
		prxcap->vLatency = LATENCY_NOT_SUPPORT;
	else
		prxcap->vLatency = (val[0] - 1) * 2;

	if (val[1] == 0)
		prxcap->aLatency = LATENCY_INVALID_UNKNOWN;
	else if (val[1] == 0xFF)
		prxcap->aLatency = LATENCY_NOT_SUPPORT;
	else
		prxcap->aLatency = (val[1] - 1) * 2;
}

static void get_ilatency(struct rx_cap *prxcap, unsigned char *val)
{
	if (val[0] == 0)
		prxcap->i_vLatency = LATENCY_INVALID_UNKNOWN;
	else if (val[0] == 0xFF)
		prxcap->i_vLatency = LATENCY_NOT_SUPPORT;
	else
		prxcap->i_vLatency = val[0] * 2 - 1;

	if (val[1] == 0)
		prxcap->i_aLatency = LATENCY_INVALID_UNKNOWN;
	else if (val[1] == 0xFF)
		prxcap->i_aLatency = LATENCY_NOT_SUPPORT;
	else
		prxcap->i_aLatency = val[1] * 2 - 1;
}

static int hdmitx_edid_block_parse(struct hdmitx_dev *hdev,
	unsigned char *blockbuf)
{
	unsigned char offset, end;
	unsigned char count;
	unsigned char tag;
	int i, tmp, idx;
	unsigned char *vfpdb_offset = NULL;
	struct rx_cap *prxcap = &hdev->rxcap;
	unsigned int aud_flag = 0;

	if (blockbuf[0] != 0x02)
		return -1; /* not a CEA BLOCK. */
	end = blockbuf[2]; /* CEA description. */
	prxcap->native_Mode = blockbuf[3];
	prxcap->number_of_dtd += blockbuf[3] & 0xf;

	prxcap->native_VIC = 0xff;

	Edid_Y420CMDB_Reset(&(hdev->hdmi_info));

	for (offset = 4 ; offset < end ; ) {
		tag = blockbuf[offset] >> 5;
		count = blockbuf[offset] & 0x1f;
		switch (tag) {
		case HDMI_EDID_BLOCK_TYPE_AUDIO:
			aud_flag = 1;
			tmp = count / 3;
			idx = prxcap->AUD_count;
			prxcap->AUD_count += tmp;
			offset++;
			for (i = 0 ; i < tmp; i++) {
				prxcap->RxAudioCap[idx + i].audio_format_code =
					(blockbuf[offset + i * 3] >> 3) & 0xf;
				prxcap->RxAudioCap[idx + i].channel_num_max =
					blockbuf[offset + i * 3] & 0x7;
				prxcap->RxAudioCap[idx + i].freq_cc =
					blockbuf[offset + i * 3 + 1] & 0x7f;
				prxcap->RxAudioCap[idx + i].cc3 =
					blockbuf[offset + i * 3 + 2];
			}
			offset += count;
			break;

		case HDMI_EDID_BLOCK_TYPE_VIDEO:
			offset++;
			for (i = 0 ; i < count ; i++) {
				unsigned char VIC;

				VIC = blockbuf[offset + i] & (~0x80);
				prxcap->VIC[prxcap->VIC_count] = VIC;
				if (blockbuf[offset + i] & 0x80)
					prxcap->native_VIC = VIC;
				prxcap->VIC_count++;
			}
			offset += count;
			break;

		case HDMI_EDID_BLOCK_TYPE_VENDER:
			offset++;
			if ((blockbuf[offset] == 0x03) &&
			    (blockbuf[offset + 1] == 0x0c) &&
			    (blockbuf[offset + 2] == 0x00)) {
				prxcap->ieeeoui = HDMI_IEEEOUI;
				prxcap->ColorDeepSupport =
					(count > 5) ? blockbuf[offset + 5] : 0;
				set_vsdb_dc_cap(prxcap);
				prxcap->Max_TMDS_Clock1 =
					(count > 6) ? blockbuf[offset + 6] : 0;
				if (count > 7) {
					tmp = blockbuf[offset + 7];
					idx = offset + 8;
					if (tmp & (1<<6)) {
						unsigned char val[2];

						val[0] = blockbuf[idx];
						val[1] = blockbuf[idx + 1];
						get_latency(prxcap, val);
						idx += 2;
					}
					if (tmp & (1<<7)) {
						unsigned char val[2];

						val[0] = blockbuf[idx];
						val[1] = blockbuf[idx + 1];
						get_ilatency(prxcap, val);
						idx += 2;
					}
					prxcap->cnc0 = (tmp >> 0) & 1;
					prxcap->cnc1 = (tmp >> 1) & 1;
					prxcap->cnc2 = (tmp >> 2) & 1;
					prxcap->cnc3 = (tmp >> 3) & 1;
					if (tmp & (1<<5)) {
						idx += 1;
						/* valid 4k */
					if (blockbuf[idx] & 0xe0) {
						hdmitx_edid_4k2k_parse(
							prxcap,
							&blockbuf[idx + 1],
							blockbuf[idx] >> 5);
					}
					/* valid 3D */
					if (blockbuf[idx - 1] & 0xe0) {
						hdmitx_edid_3d_parse(
							prxcap,
							&blockbuf[offset + 7],
							count - 7);
					}
					}
				}
			} else if ((blockbuf[offset] == 0xd8) &&
				(blockbuf[offset + 1] == 0x5d) &&
				(blockbuf[offset + 2] == 0xc4)) {
				prxcap->hf_ieeeoui = HF_IEEEOUI;
				prxcap->hdmi2ver = 0;
				prxcap->Max_TMDS_Clock2 = blockbuf[offset + 4];
				prxcap->scdc_present =
					!!(blockbuf[offset + 5] & (1 << 7));
				prxcap->scdc_rr_capable =
					!!(blockbuf[offset + 5] & (1 << 6));
				prxcap->lte_340mcsc_scramble =
					!!(blockbuf[offset + 5] & (1 << 3));
				set_vsdb_dc_420_cap(&hdev->rxcap,
						    &blockbuf[offset]);
				if (count > 7) {
					unsigned char b7 = blockbuf[offset + 7];
					prxcap->allm = !!(b7 & (1 << 1));
					prxcap->hdmi2ver = 1;
				}
			}

			offset += count; /* ignore the remaind. */
			break;

		case HDMI_EDID_BLOCK_TYPE_SPEAKER:
			offset++;
			prxcap->RxSpeakerAllocation = blockbuf[offset];
			offset += count;
			break;

		case HDMI_EDID_BLOCK_TYPE_VESA:
			offset++;
			offset += count;
			break;

		case HDMI_EDID_BLOCK_TYPE_EXTENDED_TAG:
			{
				unsigned char ext_tag = 0;
				unsigned char *t = NULL;
				unsigned char v = 0;

				ext_tag = blockbuf[offset + 1];
				switch (ext_tag) {
				case EXTENSION_VENDOR_SPECIFIC:
					t = &blockbuf[offset];
					Edid_ParsingVendSpec(hdev, prxcap, t);
					break;
				case EXTENSION_COLORMETRY_TAG:
					prxcap->colorimetry_data =
						blockbuf[offset + 2];
					break;
				case EXTENSION_DRM_STATIC_TAG:
					t = &blockbuf[offset];
					Edid_ParsingDRMStaticBlock(prxcap, t);
					v = (blockbuf[offset] & 0x1f) + 1;
					rx_set_hdr_lumi(&blockbuf[offset], v);
					break;
				case EXTENSION_DRM_DYNAMIC_TAG:
					t = &blockbuf[offset];
					Edid_ParsingDRMDynamicBlock(prxcap, t);
					break;
				case EXTENSION_VFPDB_TAG:
/* Just record VFPDB offset address, call Edid_ParsingVFPDB() after DTD
 * parsing, in case that
 * SVR >=129 and SVR <=144, Interpret as the Kth DTD in the EDID,
 * where K = SVR – 128 (for K=1 to 16)
 */
					vfpdb_offset = &blockbuf[offset];
					break;
				case EXTENSION_Y420_VDB_TAG:
					t = &blockbuf[offset];
					Edid_ParsingY420VDBBlock(prxcap, t);
					break;
				case EXTENSION_Y420_CMDB_TAG:
					Edid_ParsingY420CMDBBlock(
						&(hdev->hdmi_info),
						&blockbuf[offset]);
					break;
				default:
					break;
				}
			}
			offset += count+1;
			break;

		case HDMI_EDID_BLOCK_TYPE_RESERVED:
			offset++;
			offset += count;
			break;

		case HDMI_EDID_BLOCK_TYPE_RESERVED2:
			offset++;
			offset += count;
			break;

		default:
			break;
		}
	}

	if (aud_flag == 0)
		hdmitx_edid_set_default_aud(hdev);

	Edid_Y420CMDB_PostProcess(hdev);
	hdev->vic_count = prxcap->VIC_count;

	idx = blockbuf[3] & 0xf;
	for (i = 0; i < idx; i++)
		Edid_DTD_parsing(prxcap, &blockbuf[blockbuf[2] + i * 18]);
	if (vfpdb_offset)
		Edid_ParsingVFPDB(prxcap, vfpdb_offset);
	if ((prxcap->hdmi2ver == 1) &&
	    (prxcap->dv_info.ver == 2) &&
	    (prxcap->dv_info.sup_yuv422_12bit == 1))
		prxcap->dv_info.dv_emp_cap = 1;
	else
		prxcap->dv_info.dv_emp_cap = 0;
	return 0;
}

static void hdmitx_edid_set_default_aud(struct hdmitx_dev *hdev)
{
	struct rx_cap *prxcap = &hdev->rxcap;

	/* if AUD_count not equal to 0, no need default value */
	if (prxcap->AUD_count)
		return;

	prxcap->AUD_count = 1;
	prxcap->RxAudioCap[0].audio_format_code = 1; /* PCM */
	prxcap->RxAudioCap[0].channel_num_max = 1; /* 2ch */
	prxcap->RxAudioCap[0].freq_cc = 7; /* 32/44.1/48 kHz */
	prxcap->RxAudioCap[0].cc3 = 1; /* 16bit */
}

/* add default VICs for DVI case */
static void hdmitx_edid_set_default_vic(struct hdmitx_dev *hdmitx_device)
{
	struct rx_cap *prxcap = &hdmitx_device->rxcap;

	prxcap->VIC_count = 0x4;
	prxcap->VIC[0] = HDMI_720x480p60_16x9;
	prxcap->VIC[1] = HDMI_1280x720p60_16x9;
	prxcap->VIC[2] = HDMI_1920x1080i60_16x9;
	prxcap->VIC[3] = HDMI_1920x1080p60_16x9;
	prxcap->native_VIC = HDMI_720x480p60_16x9;
	hdmitx_device->vic_count = prxcap->VIC_count;
	pr_info(EDID "set default vic\n");
}

#if 0
#define PRINT_HASH(hash)	\
	{			\
		pr_info("%s:%d ", __func__, __LINE__); int __i;	\
		for (__i = 0; __i < 20; __i++)	\
			pr_info("%02x,", hash[__i]);	\
			pr_info("\n");\
	}
#else
#define PRINT_HASH(hash)
#endif

static int edid_hash_calc(unsigned char *hash, const char *data,
	unsigned int len)
{
#if 0
	struct scatterlist sg;

	struct crypto_hash *tfm;
	struct hash_desc desc;

	tfm = crypto_alloc_hash("sha1", 0, CRYPTO_ALG_ASYNC);
	PRINT_HASH(hash);
	if (IS_ERR(tfm))
		return -EINVAL;

	PRINT_HASH(hash);
	/* ... set up the scatterlists ... */
	sg_init_one(&sg, (u8 *) data, len);
	desc.tfm = tfm;
	desc.flags = 0;



	if (crypto_hash_digest(&desc, &sg, len, hash))
		return -EINVAL;

	err = crypto_ahash_digest(req);
	ahash_request_zero(req);

	PRINT_HASH(hash);
	crypto_free_hash(tfm);
#endif
	return 1;
}

static int hdmitx_edid_search_IEEEOUI(char *buf)
{
	int i;

	for (i = 0; i < 0x180 - 2; i++) {
		if ((buf[i] == 0x03) && (buf[i+1] == 0x0c) &&
			(buf[i+2] == 0x00))
			return 1;
	}
	return 0;
}

/* check EDID strictly */
static int edid_check_valid(unsigned char *buf)
{
	unsigned int chksum = 0;
	unsigned int i = 0;

	/* check block 0 first 8 bytes */
	if ((buf[0] != 0) && (buf[7] != 0))
		return 0;
	for (i = 1; i < 7; i++) {
		if (buf[i] != 0xff)
			return 0;
	}

	/* check block 0 checksum */
	for (chksum = 0, i = 0; i < 0x80; i++)
		chksum += buf[i];

	if ((chksum & 0xff) != 0)
		return 0;

	/* check Extension flag at block 0 */
	if (buf[0x7e] == 0)
		return 0;

	/* check block 1 extension tag */
	if (!((buf[0x80] == 0x2) || (buf[0x80] == 0xf0)))
		return 0;

	/* check block 1 checksum */
	for (chksum = 0, i = 0x80; i < 0x100; i++)
		chksum += buf[i];

	if ((chksum & 0xff) != 0)
		return 0;

	return 1;
}

/* retrun 1 valid edid */
int check_dvi_hdmi_edid_valid(unsigned char *buf)
{
	unsigned int chksum = 0;
	unsigned int i = 0;

	/* check block 0 first 8 bytes */
	if ((buf[0] != 0) && (buf[7] != 0))
		return 0;
	for (i = 1; i < 7; i++) {
		if (buf[i] != 0xff)
			return 0;
	}

	/* check block 0 checksum */
	for (chksum = 0, i = 0; i < 0x80; i++)
		chksum += buf[i];
	if ((chksum & 0xff) != 0)
		return 0;

	if (buf[0x7e] == 0)/* check Extension flag at block 0 */
		return 1;
	/* check block 1 extension tag */
	else if (!((buf[0x80] == 0x2) || (buf[0x80] == 0xf0)))
		return 0;

	/* check block 1 checksum */
	for (chksum = 0, i = 0x80; i < 0x100; i++)
		chksum += buf[i];
	if ((chksum & 0xff) != 0)
		return 0;

	/* check block 2 checksum */
	if (buf[0x7e] > 1) {
		for (chksum = 0, i = 0x100; i < 0x180; i++)
			chksum += buf[i];
		if ((chksum & 0xff) != 0)
			return 0;
	}

	/* check block 3 checksum */
	if (buf[0x7e] > 2) {
		for (chksum = 0, i = 0x180; i < 0x200; i++)
			chksum += buf[i];
		if ((chksum & 0xff) != 0)
			return 0;
	}

	return 1;
}

static void Edid_ManufactureDateParse(struct rx_cap *prxcap,
				      unsigned char *data)
{
	if (data == NULL)
		return;

	/* week:
	 *	0: not specified
	 *	0x1~0x36: valid week
	 *	0x37~0xfe: reserved
	 *	0xff: model year is specified
	 */
	if ((data[0] == 0) || ((data[0] >= 0x37) && (data[0] <= 0xfe)))
		prxcap->manufacture_week = 0;
	else
		prxcap->manufacture_week = data[0];

	/* year:
	 *	0x0~0xf: reserved
	 *	0x10~0xff: year of manufacture,
	 *		or model year(if specified by week=0xff)
	 */
	prxcap->manufacture_year =
		(data[1] <= 0xf)?0:data[1];
}

static void Edid_VersionParse(struct rx_cap *prxcap,
			      unsigned char *data)
{
	if (data == NULL)
		return;

	/*
	 *	0x1: edid version 1
	 *	0x0,0x2~0xff: reserved
	 */
	prxcap->edid_version = (data[0] == 0x1) ? 1 : 0;

	/*
	 *	0x0~0x4: revision number
	 *	0x5~0xff: reserved
	 */
	prxcap->edid_revision = (data[1] < 0x5) ? data[1] : 0;
}

static void Edid_PhyscialSizeParse(struct rx_cap *pRxCap,
		unsigned char *data)
{
	if ((data[0] != 0) && (data[1] != 0)) {
		pRxCap->physcial_weight = data[0];
		pRxCap->physcial_height = data[1];
	}
}

/* if edid block 0 are all zeros, then consider RX as HDMI device */
static int edid_zero_data(unsigned char *buf)
{
	int sum = 0;
	int i = 0;

	for (i = 0; i < 128; i++)
		sum += buf[i];

	if (sum == 0)
		return 1;
	else
		return 0;
}

static void dump_dtd_info(struct dtd *t)
{
	pr_info(EDID "%s[%d]\n", __func__, __LINE__);
#define PR(a) pr_info(EDID "%s: %d\n", #a, t->a)
	PR(pixel_clock);
	PR(h_active);
	PR(h_blank);
	PR(v_active);
	PR(v_blank);
	PR(h_sync_offset);
	PR(h_sync);
	PR(v_sync_offset);
	PR(v_sync);
}

static void Edid_DTD_parsing(struct rx_cap *prxcap, unsigned char *data)
{
	struct hdmi_format_para *para = NULL;
	struct dtd *t = &prxcap->dtd[prxcap->dtd_idx];

	memset(t, 0, sizeof(struct dtd));
	t->pixel_clock = data[0] + (data[1] << 8);
	t->h_active = (((data[4] >> 4) & 0xf) << 8) + data[2];
	t->h_blank = ((data[4] & 0xf) << 8) + data[3];
	t->v_active = (((data[7] >> 4) & 0xf) << 8) + data[5];
	t->v_blank = ((data[7] & 0xf) << 8) + data[6];
	t->h_sync_offset = (((data[11] >> 6) & 0x3) << 8) + data[8];
	t->h_sync = (((data[11] >> 4) & 0x3) << 8) + data[9];
	t->v_sync_offset = (((data[11] >> 2) & 0x3) << 4) +
		((data[10] >> 4) & 0xf);
	t->v_sync = (((data[11] >> 0) & 0x3) << 4) + ((data[10] >> 0) & 0xf);
/*
 * Special handling of 1080i60hz, 1080i50hz
 */
	if ((t->pixel_clock == 7425) && (t->h_active == 1920) &&
		(t->v_active == 1080)) {
		t->v_active = t->v_active / 2;
		t->v_blank = t->v_blank / 2;
	}
/*
 * Special handling of 480i60hz, 576i50hz
 */
	if (((((t->flags) >> 1) & 0x3) == 0) && (t->h_active == 1440)) {
		if (t->pixel_clock == 2700) /* 576i50hz */
			goto next;
		if ((t->pixel_clock - 2700) < 10) /* 480i60hz */
			t->pixel_clock = 2702;
next:
		t->v_active = t->v_active / 2;
		t->v_blank = t->v_blank / 2;
	}
/*
 * call hdmi_match_dtd_paras() to check t is matched with VIC
 */
	para = hdmi_match_dtd_paras(t);
	if (para) {
		t->vic = para->vic;
		prxcap->preferred_mode = prxcap->dtd[0].vic; /* Select dtd0 */
		pr_info(EDID "get dtd%d vic: %d\n",
			prxcap->dtd_idx, para->vic);
		prxcap->dtd_idx++;
	} else
		if (0) /* for debug usage */
			dump_dtd_info(t);
}

static void edid_check_pcm_declare(struct rx_cap *prxcap)
{
	int idx_pcm = 0;
	int i;

	if (!prxcap->AUD_count)
		return;

	/* Try to find more than 1 PCMs, RxAudioCap[0] is always basic audio */
	for (i = 1; i < prxcap->AUD_count; i++) {
		if (prxcap->RxAudioCap[i].audio_format_code ==
			prxcap->RxAudioCap[0].audio_format_code) {
			idx_pcm = i;
			break;
		}
	}

	/* Remove basic audio */
	if (idx_pcm) {
		for (i = 0; i < prxcap->AUD_count - 1; i++)
			memcpy(&prxcap->RxAudioCap[i],
			       &prxcap->RxAudioCap[i + 1],
			       sizeof(struct rx_audiocap));
		/* Clear the last audio declaration */
		memset(&prxcap->RxAudioCap[i], 0, sizeof(struct rx_audiocap));
		prxcap->AUD_count--;
	}
}

static bool is_4k60_supported(struct rx_cap *prxcap)
{
	int i = 0;

	if (!prxcap)
		return false;

	for (i = 0; (i < prxcap->VIC_count) && (i < VIC_MAX_NUM); i++) {
		if (((prxcap->VIC[i] & 0xff) == HDMI_3840x2160p50_16x9) ||
		    ((prxcap->VIC[i] & 0xff) == HDMI_3840x2160p60_16x9)) {
			return true;
		}
	}
	return false;
}

static void Edid_Descriptor_PMT(struct rx_cap *prxcap,
				struct vesa_standard_timing *t,
				unsigned char *data)
{
	struct hdmi_format_para *para = NULL;

	t->tmds_clk = data[0] + (data[1] << 8);
	t->hactive = data[2] + (((data[4] >> 4) & 0xf) << 8);
	t->hblank = data[3] + ((data[4] & 0xf) << 8);
	t->vactive = data[5] + (((data[7] >> 4) & 0xf) << 8);
	t->vblank = data[6] + ((data[7] & 0xf) << 8);
	para = hdmi_get_vesa_paras(t);
	if (para && ((para->vic) < (HDMI_3840x2160p60_64x27 + 1))) {
		prxcap->native_VIC = para->vic;
		pr_info("hdmitx: get PMT vic: %d\n", para->vic);
	}
	if (para && ((para->vic) >= HDMITX_VESA_OFFSET))
		store_vesa_idx(prxcap, para->vic);
}

static void Edid_Descriptor_PMT2(struct rx_cap *prxcap,
				 struct vesa_standard_timing *t,
				 unsigned char *data)
{
	struct hdmi_format_para *para = NULL;

	t->tmds_clk = data[0] + (data[1] << 8);
	t->hactive = data[2] + (((data[4] >> 4) & 0xf) << 8);
	t->hblank = data[3] + ((data[4] & 0xf) << 8);
	t->vactive = data[5] + (((data[7] >> 4) & 0xf) << 8);
	t->vblank = data[6] + ((data[7] & 0xf) << 8);
	para = hdmi_get_vesa_paras(t);
	if (para && ((para->vic) >= HDMITX_VESA_OFFSET))
		store_vesa_idx(prxcap, para->vic);
}

static void Edid_CVT_timing_3bytes(struct rx_cap *prxcap,
				   struct vesa_standard_timing *t,
				   const unsigned char *data)
{
	struct hdmi_format_para *para = NULL;

	t->hactive = ((data[0] + (((data[1] >> 4) & 0xf) << 8)) + 1) * 2;
	switch ((data[1] >> 2) & 0x3) {
	case 0:
		t->vactive = t->hactive * 3 / 4;
		break;
	case 1:
		t->vactive = t->hactive * 9 / 16;
		break;
	case 2:
		t->vactive = t->hactive * 5 / 8;
		break;
	case 3:
	default:
		t->vactive = t->hactive * 3 / 5;
		break;
	}
	switch ((data[2] >> 5) & 0x3) {
	case 0:
		t->hsync = 50;
		break;
	case 1:
		t->hsync = 60;
		break;
	case 2:
		t->hsync = 75;
		break;
	case 3:
	default:
		t->hsync = 85;
		break;
	}
	para = hdmi_get_vesa_paras(t);
	if (para)
		t->vesa_timing = para->vic;
}

static void Edid_CVT_timing(struct rx_cap *prxcap, unsigned char *data)
{
	int i;
	struct vesa_standard_timing t;

	for (i = 0; i < 4; i++) {
		memset(&t, 0, sizeof(struct vesa_standard_timing));
		Edid_CVT_timing_3bytes(prxcap, &t, &data[i * 3]);
		if (t.vesa_timing)
			store_vesa_idx(prxcap, t.vesa_timing);
	}
}

static void check_dv_truly_support(struct hdmitx_dev *hdev, struct dv_info *dv)
{
	struct rx_cap *prxcap = &hdev->rxcap;
	unsigned int max_tmds_clk = 0;

	if ((dv->ieeeoui == DV_IEEE_OUI) && (dv->ver <= 2)) {
		/* check max tmds rate to determine if 4k60 DV can truly be
		 * supported.
		 */
		if (prxcap->Max_TMDS_Clock2) {
			max_tmds_clk = prxcap->Max_TMDS_Clock2 * 5;
		} else {
			/* Default min is 74.25 / 5 */
			if (prxcap->Max_TMDS_Clock1 < 0xf)
				prxcap->Max_TMDS_Clock1 = 0x1e;
			max_tmds_clk = prxcap->Max_TMDS_Clock1 * 5;
		}
		if (dv->ver == 0)
			dv->sup_2160p60hz = dv->sup_2160p60hz &&
						(max_tmds_clk >= 594);

		if ((dv->ver == 1) && (dv->length == 0xB)) {
			if (dv->low_latency == 0x00) {
				/*standard mode */
				dv->sup_2160p60hz = dv->sup_2160p60hz &&
							(max_tmds_clk >= 594);
			} else if (dv->low_latency == 0x01) {
				/* both standard and LL are supported. 4k60 LL
				 * DV support should/can be determined using
				 * video formats supported inthe E-EDID as flag
				 * sup_2160p60hz might not be set.
				 */
				if ((dv->sup_2160p60hz ||
				     is_4k60_supported(prxcap)) &&
				     (max_tmds_clk >= 594))
					dv->sup_2160p60hz = 1;
				else
					dv->sup_2160p60hz = 0;
			}
		}

		if ((dv->ver == 1) && (dv->length == 0xE))
			dv->sup_2160p60hz = dv->sup_2160p60hz &&
						(max_tmds_clk >= 594);

		if (dv->ver == 2) {
			/* 4k60 DV support should be determined using video
			 * formats supported in the EEDID as flag sup_2160p60hz
			 * is not applicable for VSVDB V2.
			 */
			if (is_4k60_supported(prxcap) && (max_tmds_clk >= 594))
				dv->sup_2160p60hz = 1;
			else
				dv->sup_2160p60hz = 0;
		}
	}
}

int hdmitx_edid_parse(struct hdmitx_dev *hdmitx_device)
{
	unsigned char CheckSum;
	unsigned char zero_numbers;
	unsigned char BlockCount;
	unsigned char *EDID_buf;
	int i, j, ret_val;
	int idx[4];
	struct rx_cap *prxcap = &hdmitx_device->rxcap;
	struct dv_info *dv = &hdmitx_device->rxcap.dv_info;

	if (check_dvi_hdmi_edid_valid(hdmitx_device->EDID_buf)) {
		EDID_buf = hdmitx_device->EDID_buf;
		hdmitx_device->edid_parsing = 1;
		memcpy(hdmitx_device->EDID_buf1, hdmitx_device->EDID_buf,
			EDID_MAX_BLOCK * 128);
	} else
		EDID_buf = hdmitx_device->EDID_buf1;

	if (check_dvi_hdmi_edid_valid(hdmitx_device->EDID_buf1))
		hdmitx_device->edid_parsing = 1;

	hdmitx_device->edid_ptr = EDID_buf;
	pr_info(EDID "EDID Parser:\n");
	/* Calculate the EDID hash for special use */
	memset(hdmitx_device->EDID_hash, 0,
		ARRAY_SIZE(hdmitx_device->EDID_hash));
	edid_hash_calc(hdmitx_device->EDID_hash, hdmitx_device->EDID_buf, 256);

	ret_val = Edid_DecodeHeader(&hdmitx_device->hdmi_info, &EDID_buf[0]);

	for (i = 0, CheckSum = 0 ; i < 128 ; i++) {
		CheckSum += EDID_buf[i];
		CheckSum &= 0xFF;
	}

	if (CheckSum != 0)
		pr_info(EDID "PLUGIN_DVI_OUT\n");

	Edid_ParsingIDManufacturerName(&hdmitx_device->rxcap, &EDID_buf[8]);
	Edid_ParsingIDProductCode(&hdmitx_device->rxcap, &EDID_buf[0x0A]);
	Edid_ParsingIDSerialNumber(&hdmitx_device->rxcap, &EDID_buf[0x0C]);

	Edid_EstablishedTimings(&hdmitx_device->rxcap, &EDID_buf[0x23]);
	Edid_StandardTiming(&hdmitx_device->rxcap, &EDID_buf[0x26], 8);

	Edid_ManufactureDateParse(&hdmitx_device->rxcap, &EDID_buf[16]);

	Edid_VersionParse(&hdmitx_device->rxcap, &EDID_buf[18]);

	Edid_PhyscialSizeParse(&hdmitx_device->rxcap, &EDID_buf[21]);

	Edid_DecodeStandardTiming(&hdmitx_device->hdmi_info, &EDID_buf[26], 8);
	Edid_ParseCEADetailedTimingDescriptors(&hdmitx_device->hdmi_info,
		4, 0x36, &EDID_buf[0]);

	BlockCount = EDID_buf[0x7E];
	hdmitx_device->rxcap.blk0_chksum = EDID_buf[0x7F];

	if (BlockCount == 0) {
		pr_info(EDID "EDID BlockCount=0\n");
		hdmitx_edid_set_default_vic(hdmitx_device);

		/* DVI case judgement: only contains one block and
		 * checksum valid
		 */
		CheckSum = 0;
		zero_numbers = 0;
		for (i = 0; i < 128; i++) {
			CheckSum += EDID_buf[i];
			if (EDID_buf[i] == 0)
				zero_numbers++;
		}
		pr_info(EDID "edid blk0 checksum:%d ext_flag:%d\n",
			CheckSum, EDID_buf[0x7e]);
		if ((CheckSum & 0xff) == 0)
			hdmitx_device->rxcap.ieeeoui = 0;
		else
			hdmitx_device->rxcap.ieeeoui = HDMI_IEEEOUI;
		if (zero_numbers > 120)
			hdmitx_device->rxcap.ieeeoui = HDMI_IEEEOUI;

		return 0; /* do nothing. */
	}

	/* Note: some DVI monitor have more than 1 block */
	if ((BlockCount == 1) && (EDID_buf[0x81] == 1)) {
		hdmitx_device->rxcap.ieeeoui = 0;
		hdmitx_device->rxcap.VIC_count = 0x3;
		hdmitx_device->rxcap.VIC[0] = HDMI_720x480p60_16x9;
		hdmitx_device->rxcap.VIC[1] = HDMI_1280x720p60_16x9;
		hdmitx_device->rxcap.VIC[2] = HDMI_1920x1080p60_16x9;
		hdmitx_device->rxcap.native_VIC = HDMI_720x480p60_16x9;
		hdmitx_device->vic_count = hdmitx_device->rxcap.VIC_count;
		pr_info(EDID "set default vic\n");
		return 0;
	} else if (BlockCount > EDID_MAX_BLOCK) {
		BlockCount = EDID_MAX_BLOCK;
	}

	for (i = 1 ; i <= BlockCount ; i++) {
		if ((BlockCount > 1) && (i == 1))
			CheckSum = 0;	   /* ignore the block1 data */
		else {
			if (((BlockCount == 1) && (i == 1)) ||
				((BlockCount > 1) && (i == 2)))
				Edid_Parse_check_HDMI_VSDB(
					hdmitx_device,
					&EDID_buf[i * 128]);

			for (j = 0, CheckSum = 0 ; j < 128 ; j++) {
				CheckSum += EDID_buf[i*128 + j];
				CheckSum &= 0xFF;
			}
			if (CheckSum == 0) {
				Edid_MonitorCapable861(
					&hdmitx_device->hdmi_info,
					EDID_buf[i * 128 + 3]);
				ret_val = Edid_ParsingCEADataBlockCollection(
					hdmitx_device, &EDID_buf[i * 128]);
				Edid_ParseCEADetailedTimingDescriptors(
					&hdmitx_device->hdmi_info, 5,
					EDID_buf[i * 128 + 2],
					&EDID_buf[i * 128]);
			}
		}

		hdmitx_edid_block_parse(hdmitx_device, &(EDID_buf[i*128]));
	}

	/* EDID parsing complete - check if 4k60/50 DV can be truly supported */
	dv = &prxcap->dv_info;
	check_dv_truly_support(hdmitx_device, dv);
	dv = &prxcap->dv_info2;
	check_dv_truly_support(hdmitx_device, dv);

	edid_check_pcm_declare(&hdmitx_device->rxcap);
/*
 * Because DTDs are not able to represent some Video Formats, which can be
 * represented as SVDs and might be preferred by Sinks, the first DTD in the
 * base EDID data structure and the first SVD in the first CEA Extension can
 * differ. When the first DTD and SVD do not match and the total number of
 * DTDs defining Native Video Formats in the whole EDID is zero, the first
 * SVD shall take precedence.
 */
	if (!prxcap->flag_vfpdb && (prxcap->preferred_mode != prxcap->VIC[0]) &&
	    (prxcap->number_of_dtd == 0)) {
		pr_info(EDID "change preferred_mode from %d to %d\n",
			prxcap->preferred_mode,	prxcap->VIC[0]);
		prxcap->preferred_mode = prxcap->VIC[0];
	}

	idx[0] = EDID_DETAILED_TIMING_DES_BLOCK0_POS;
	idx[1] = EDID_DETAILED_TIMING_DES_BLOCK1_POS;
	idx[2] = EDID_DETAILED_TIMING_DES_BLOCK2_POS;
	idx[3] = EDID_DETAILED_TIMING_DES_BLOCK3_POS;
	for (i = 0; i < 4; i++) {
		if ((EDID_buf[idx[i]]) && (EDID_buf[idx[i] + 1])) {
			struct vesa_standard_timing t;

			memset(&t, 0, sizeof(struct vesa_standard_timing));
			if (i == 0)
				Edid_Descriptor_PMT(prxcap, &t,
						    &EDID_buf[idx[i]]);
			if (i == 1)
				Edid_Descriptor_PMT2(prxcap, &t,
						     &EDID_buf[idx[i]]);
			continue;
		}
		switch (EDID_buf[idx[i] + 3]) {
		case TAG_STANDARD_TIMINGS:
			Edid_StandardTiming(prxcap, &EDID_buf[idx[i] + 5], 6);
			break;
		case TAG_CVT_TIMING_CODES:
			Edid_CVT_timing(prxcap, &EDID_buf[idx[i] + 6]);
			break;
		case TAG_ESTABLISHED_TIMING_III:
			Edid_StandardTimingIII(prxcap, &EDID_buf[idx[i] + 6]);
			break;
		case TAG_RANGE_LIMITS:
			break;
		case TAG_DISPLAY_PRODUCT_NAME_STRING:
			Edid_ReceiverProductNameParse(prxcap,
						      &EDID_buf[idx[i] + 5]);
			break;
		default:
			break;
		}
	}

	if (hdmitx_edid_search_IEEEOUI(&EDID_buf[128])) {
		prxcap->ieeeoui = HDMI_IEEEOUI;
		pr_info(EDID "find IEEEOUT\n");
	} else {
		prxcap->ieeeoui = 0x0;
		pr_info(EDID "not find IEEEOUT\n");
	}

	if ((prxcap->ieeeoui != HDMI_IEEEOUI) || (prxcap->ieeeoui == 0x0) ||
	    (prxcap->VIC_count == 0))
		hdmitx_edid_set_default_vic(hdmitx_device);

	/* strictly DVI device judgement */
	/* valid EDID & no audio tag & no IEEEOUI */
	if (edid_check_valid(&EDID_buf[0]) &&
		!hdmitx_edid_search_IEEEOUI(&EDID_buf[128])) {
		prxcap->ieeeoui = 0x0;
		pr_info(EDID "sink is DVI device\n");
	} else
		prxcap->ieeeoui = HDMI_IEEEOUI;

	if (edid_zero_data(EDID_buf))
		prxcap->ieeeoui = HDMI_IEEEOUI;

	if ((!prxcap->AUD_count) && (!prxcap->ieeeoui))
		hdmitx_edid_set_default_aud(hdmitx_device);

	edid_save_checkvalue(EDID_buf, BlockCount + 1, prxcap);

	i = hdmitx_edid_dump(hdmitx_device, (char *)(hdmitx_device->tmp_buf),
		HDMI_TMP_BUF_SIZE);
	hdmitx_device->tmp_buf[i] = 0;

	if (!hdmitx_edid_check_valid_blocks(&EDID_buf[0])) {
		prxcap->ieeeoui = HDMI_IEEEOUI;
		pr_info(EDID "Invalid edid, consider RX as HDMI device\n");
	}
	dv = &prxcap->dv_info;
	/* if sup_2160p60hz of dv or dv2 is true, check the MAX_TMDS*/
	if (dv->sup_2160p60hz) {
		if (prxcap->Max_TMDS_Clock2 * 5 < 590) {
			dv->sup_2160p60hz = 0;
			pr_info(EDID "clear sup_2160p60hz\n");
		}
	}
	dv = &prxcap->dv_info2;
	if (dv->sup_2160p60hz) {
		if (prxcap->Max_TMDS_Clock2 * 5 < 590) {
			dv->sup_2160p60hz = 0;
			pr_info(EDID "clear sup_2160p60hz\n");
		}
	}
	return 0;

}

static struct dispmode_vic dispmode_vic_tab[] = {
	{"480i60hz", HDMI_480i60_16x9},
	{"480p60hz", HDMI_480p60_16x9},
	{"576i50hz", HDMI_576i50_16x9},
	{"576p50hz", HDMI_576p50_16x9},
	{"720p50hz", HDMI_720p50},
	{"720p60hz", HDMI_720p60},
	{"1080i50hz", HDMI_1080i50},
	{"1080i60hz", HDMI_1080i60},
	{"1080p50hz", HDMI_1080p50},
	{"1080p30hz", HDMI_1080p30},
	{"1080p25hz", HDMI_1080p25},
	{"1080p24hz", HDMI_1080p24},
	{"1080p60hz", HDMI_1080p60},
	{"2560x1080p50hz", HDMI_2560x1080p50_64x27},
	{"2560x1080p60hz", HDMI_2560x1080p60_64x27},
	{"2160p30hz", HDMI_4k2k_30},
	{"2160p25hz", HDMI_4k2k_25},
	{"2160p24hz", HDMI_4k2k_24},
	{"smpte24hz", HDMI_4k2k_smpte_24},
	{"smpte25hz", HDMI_4096x2160p25_256x135},
	{"smpte30hz", HDMI_4096x2160p30_256x135},
	{"smpte50hz420", HDMI_4096x2160p50_256x135_Y420},
	{"smpte60hz420", HDMI_4096x2160p60_256x135_Y420},
	{"2160p60hz420", HDMI_3840x2160p60_16x9_Y420},
	{"2160p50hz420", HDMI_3840x2160p50_16x9_Y420},
	{"smpte50hz", HDMI_4096x2160p50_256x135},
	{"smpte60hz", HDMI_4096x2160p60_256x135},
	{"2160p60hz", HDMI_4k2k_60},
	{"2160p50hz", HDMI_4k2k_50},
	{"640x480p60hz", HDMIV_640x480p60hz},
	{"800x480p60hz", HDMIV_800x480p60hz},
	{"800x600p60hz", HDMIV_800x600p60hz},
	{"852x480p60hz", HDMIV_852x480p60hz},
	{"854x480p60hz", HDMIV_854x480p60hz},
	{"1024x600p60hz", HDMIV_1024x600p60hz},
	{"1024x768p60hz", HDMIV_1024x768p60hz},
	{"1152x864p75hz", HDMIV_1152x864p75hz},
	{"1280x600p60hz", HDMIV_1280x600p60hz},
	{"1280x768p60hz", HDMIV_1280x768p60hz},
	{"1280x800p60hz", HDMIV_1280x800p60hz},
	{"1280x960p60hz", HDMIV_1280x960p60hz},
	{"1280x1024p60hz", HDMIV_1280x1024p60hz},
	{"1280x1024", HDMIV_1280x1024p60hz}, /* alias of "1280x1024p60hz" */
	{"1360x768p60hz", HDMIV_1360x768p60hz},
	{"1366x768p60hz", HDMIV_1366x768p60hz},
	{"1400x1050p60hz", HDMIV_1400x1050p60hz},
	{"1440x900p60hz", HDMIV_1440x900p60hz},
	{"1440x2560p60hz", HDMIV_1440x2560p60hz},
	{"1600x900p60hz", HDMIV_1600x900p60hz},
	{"1600x1200p60hz", HDMIV_1600x1200p60hz},
	{"1680x1050p60hz", HDMIV_1680x1050p60hz},
	{"1920x1200p60hz", HDMIV_1920x1200p60hz},
	{"2160x1200p90hz", HDMIV_2160x1200p90hz},
	{"2560x1080p60hz", HDMIV_2560x1080p60hz},
	{"2560x1440p60hz", HDMIV_2560x1440p60hz},
	{"2560x1600p60hz", HDMIV_2560x1600p60hz},
	{"3440x1440p60hz", HDMIV_3440x1440p60hz},
};

int hdmitx_edid_VIC_support(enum hdmi_vic vic)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dispmode_vic_tab); i++) {
		if (vic == dispmode_vic_tab[i].VIC)
			return 1;
	}

	return 0;
}

enum hdmi_vic hdmitx_edid_vic_tab_map_vic(const char *disp_mode)
{
	enum hdmi_vic vic = HDMI_Unknown;
	int i;

	for (i = 0; i < ARRAY_SIZE(dispmode_vic_tab); i++) {
		if (strncmp(disp_mode, dispmode_vic_tab[i].disp_mode,
			strlen(dispmode_vic_tab[i].disp_mode)) == 0) {
			vic = dispmode_vic_tab[i].VIC;
			break;
		}
	}

	if (vic == HDMI_Unknown)
		pr_info(EDID "not find mapped vic\n");

	return vic;
}

const char *hdmitx_edid_vic_tab_map_string(enum hdmi_vic vic)
{
	int i;
	const char *disp_str = NULL;

	for (i = 0; i < ARRAY_SIZE(dispmode_vic_tab); i++) {
		if (vic == dispmode_vic_tab[i].VIC) {
			disp_str = dispmode_vic_tab[i].disp_mode;
			break;
		}
	}

	return disp_str;
}

const char *hdmitx_edid_vic_to_string(enum hdmi_vic vic)
{
	int i;
	const char *disp_str = NULL;

	for (i = 0; i < ARRAY_SIZE(dispmode_vic_tab); i++) {
		if (vic == dispmode_vic_tab[i].VIC) {
			disp_str = dispmode_vic_tab[i].disp_mode;
			break;
		}
	}

	return disp_str;
}

static bool is_rx_support_y420(struct hdmitx_dev *hdev)
{
	enum hdmi_vic vic = HDMI_Unknown;

	vic = hdmitx_edid_get_VIC(hdev, "2160p60hz420", 0);
	if (vic != HDMI_Unknown)
		return 1;

	vic = hdmitx_edid_get_VIC(hdev, "2160p50hz420", 0);
	if (vic != HDMI_Unknown)
		return 1;

	vic = hdmitx_edid_get_VIC(hdev, "smpte60hz420", 0);
	if (vic != HDMI_Unknown)
		return 1;

	vic = hdmitx_edid_get_VIC(hdev, "smpte50hz420", 0);
	if (vic != HDMI_Unknown)
		return 1;

	return 0;
}

/* For some TV's EDID, there maybe exist some information ambiguous.
 * Such as EDID declears support 2160p60hz(Y444 8bit), but no valid
 * Max_TMDS_Clock2 to indicate that it can support 5.94G signal.
 */
bool hdmitx_edid_check_valid_mode(struct hdmitx_dev *hdev,
	struct hdmi_format_para *para)
{
	bool valid = 0;
	struct rx_cap *prxcap = NULL;
	const struct dv_info *dv = &hdev->rxcap.dv_info;
	unsigned int rx_max_tmds_clk = 0;
	unsigned int calc_tmds_clk = 0;
	int i = 0;
	int svd_flag = 0;
	/* Default max color depth is 24 bit */
	enum hdmi_color_depth rx_y444_max_dc = COLORDEPTH_24B;
	enum hdmi_color_depth rx_y422_max_dc = COLORDEPTH_24B;
	enum hdmi_color_depth rx_y420_max_dc = COLORDEPTH_24B;
	enum hdmi_color_depth rx_rgb_max_dc = COLORDEPTH_24B;

	if (!hdev || !para)
		return 0;

	if (strcmp(para->sname, "invalid") == 0)
		return 0;
	/* exclude such as: 2160p60hz YCbCr444 10bit */
	switch (para->vic) {
	case HDMI_3840x2160p50_16x9:
	case HDMI_3840x2160p60_16x9:
	case HDMI_4096x2160p50_256x135:
	case HDMI_4096x2160p60_256x135:
	case HDMI_3840x2160p50_64x27:
	case HDMI_3840x2160p60_64x27:
		if ((para->cs == COLORSPACE_RGB444) ||
			(para->cs == COLORSPACE_YUV444))
			if (para->cd != COLORDEPTH_24B)
				return 0;
		break;
	default:
		break;
	}

	prxcap = &hdev->rxcap;

	/* DVI case, only 8bit */
	if (prxcap->ieeeoui != HDMI_IEEEOUI) {
		if (para->cd != COLORDEPTH_24B)
			return 0;
	}

	/* target mode is not contained at RX SVD */
	for (i = 0; (i < prxcap->VIC_count) && (i < VIC_MAX_NUM); i++) {
		if ((para->vic & 0xff) == (prxcap->VIC[i] & 0xff))
			svd_flag = 1;
	}
	if (svd_flag == 0)
		return 0;

	/* Get RX Max_TMDS_Clock */
	if (prxcap->Max_TMDS_Clock2) {
		rx_max_tmds_clk = prxcap->Max_TMDS_Clock2 * 5;
	} else {
		/* Default min is 74.25 / 5 */
		if (prxcap->Max_TMDS_Clock1 < 0xf)
			prxcap->Max_TMDS_Clock1 = 0x1e;
		rx_max_tmds_clk = prxcap->Max_TMDS_Clock1 * 5;
	}

	calc_tmds_clk = para->tmds_clk;
	if (para->cs == COLORSPACE_YUV420)
		calc_tmds_clk = calc_tmds_clk / 2;
	if (para->cs != COLORSPACE_YUV422) {
		switch (para->cd) {
		case COLORDEPTH_30B:
			calc_tmds_clk = calc_tmds_clk * 5 / 4;
			break;
		case COLORDEPTH_36B:
			calc_tmds_clk = calc_tmds_clk * 3 / 2;
			break;
		case COLORDEPTH_48B:
			calc_tmds_clk = calc_tmds_clk * 2;
			break;
		case COLORDEPTH_24B:
		default:
			calc_tmds_clk = calc_tmds_clk * 1;
			break;
		}
	}
	calc_tmds_clk = calc_tmds_clk / 1000;
	pr_info("RX tmds clk: %d   Calc clk: %d\n", rx_max_tmds_clk,
		calc_tmds_clk);
	if (calc_tmds_clk < rx_max_tmds_clk)
		valid = 1;
	else
		return 0;

	if (para->cs == COLORSPACE_YUV444) {
		/* Rx may not support Y444 */
		if (!(prxcap->native_Mode & (1 << 5)))
			return 0;
		if ((prxcap->dc_y444 && prxcap->dc_30bit)
			|| (dv->sup_10b_12b_444 == 0x1))
			rx_y444_max_dc = COLORDEPTH_30B;
		if ((prxcap->dc_y444 && prxcap->dc_36bit)
			|| (dv->sup_10b_12b_444 == 0x2))
			rx_y444_max_dc = COLORDEPTH_36B;
		if (para->cd <= rx_y444_max_dc)
			valid = 1;
		else
			valid = 0;
		return valid;
	}
	if (para->cs == COLORSPACE_YUV422) {
		/* Rx may not support Y422 */
		if (!(prxcap->native_Mode & (1 << 4)))
			return 0;
		if (prxcap->dc_y444 && prxcap->dc_30bit)
			rx_y422_max_dc = COLORDEPTH_30B;
		if ((prxcap->dc_y444 && prxcap->dc_36bit)
			|| (dv->sup_yuv422_12bit))
			rx_y422_max_dc = COLORDEPTH_36B;
		if (para->cd <= rx_y422_max_dc)
			valid = 1;
		else
			valid = 0;
		return valid;
	}
	if (para->cs == COLORSPACE_RGB444) {
		/* Always assume RX supports RGB444 */
		if ((prxcap->dc_30bit) || (dv->sup_10b_12b_444 == 0x1))
			rx_rgb_max_dc = COLORDEPTH_30B;
		if ((prxcap->dc_36bit) || (dv->sup_10b_12b_444 == 0x2))
			rx_rgb_max_dc = COLORDEPTH_36B;
		if (para->cd <= rx_rgb_max_dc)
			valid = 1;
		else
			valid = 0;
		return valid;
	}
	if (para->cs == COLORSPACE_YUV420) {
		if (!is_rx_support_y420(hdev))
			return 0;
		if (prxcap->dc_30bit_420)
			rx_y420_max_dc = COLORDEPTH_30B;
		if (prxcap->dc_36bit_420)
			rx_y420_max_dc = COLORDEPTH_36B;
		if (para->cd <= rx_y420_max_dc)
			valid = 1;
		else
			valid = 0;
		return valid;
	}

	return valid;
}

/* force_flag: 0 means check with RX's edid */
/* 1 means no check wich RX's edid */
enum hdmi_vic hdmitx_edid_get_VIC(struct hdmitx_dev *hdev,
	const char *disp_mode, char force_flag)
{
	struct rx_cap *prxcap = &hdev->rxcap;
	int  j;
	enum hdmi_vic vic = hdmitx_edid_vic_tab_map_vic(disp_mode);
	struct hdmi_format_para *para = NULL;
	enum hdmi_vic *vesa_t = &hdev->rxcap.vesa_timing[0];
	enum hdmi_vic vesa_vic;

	if (vic >= HDMITX_VESA_OFFSET)
		vesa_vic = vic;
	else
		vesa_vic = HDMI_Unknown;
	if (vic != HDMI_Unknown) {
		if (force_flag == 0) {
			for (j = 0 ; j < prxcap->VIC_count ; j++) {
				if (prxcap->VIC[j] == vic)
					break;
			}
			if (j >= prxcap->VIC_count)
				vic = HDMI_Unknown;
		}
	}
	if ((vic == HDMI_Unknown) &&
		(vesa_vic != HDMI_Unknown)) {
		for (j = 0; vesa_t[j] && j < VESA_MAX_TIMING; j++) {
			para = hdmi_get_fmt_paras(vesa_t[j]);
			if (para) {
				if ((para->vic >= HDMITX_VESA_OFFSET) &&
					(vesa_vic == para->vic)) {
					vic = para->vic;
					break;
				}
			}
		}
	}
	return vic;
}

const char *hdmitx_edid_get_native_VIC(struct hdmitx_dev *hdmitx_device)
{
	struct rx_cap *prxcap = &hdmitx_device->rxcap;

	return hdmitx_edid_vic_to_string(prxcap->native_VIC);
}

/* Clear HDMI Hardware Module EDID RAM and EDID Buffer */
void hdmitx_edid_ram_buffer_clear(struct hdmitx_dev *hdmitx_device)
{
	unsigned int i = 0;

	/* Clear HDMI Hardware Module EDID RAM */
	hdmitx_device->hwop.cntlddc(hdmitx_device, DDC_EDID_CLEAR_RAM, 0);

	/* Clear EDID Buffer */
	for (i = 0; i < EDID_MAX_BLOCK*128; i++)
		hdmitx_device->EDID_buf[i] = 0;
	for (i = 0; i < EDID_MAX_BLOCK*128; i++)
		hdmitx_device->EDID_buf1[i] = 0;
}

/* Clear the Parse result of HDMI Sink's EDID. */
void hdmitx_edid_clear(struct hdmitx_dev *hdmitx_device)
{
	char tmp[2] = {0};
	struct rx_cap *prxcap = &hdmitx_device->rxcap;

	memset(prxcap, 0, sizeof(struct rx_cap));

	/* Note: in most cases, we think that rx is tv and the default
	 * IEEEOUI is HDMI Identifier
	 */
	prxcap->ieeeoui = HDMI_IEEEOUI;

	hdmitx_device->vic_count = 0;
	hdmitx_device->hdmi_info.vsdb_phy_addr.a = 0;
	hdmitx_device->hdmi_info.vsdb_phy_addr.b = 0;
	hdmitx_device->hdmi_info.vsdb_phy_addr.c = 0;
	hdmitx_device->hdmi_info.vsdb_phy_addr.d = 0;
	hdmitx_device->hdmi_info.vsdb_phy_addr.valid = 0;
	memset(&vsdb_local, 0, sizeof(struct vsdb_phyaddr));
	memset(&hdmitx_device->EDID_hash[0], 0,
		sizeof(hdmitx_device->EDID_hash));
	hdmitx_device->edid_parsing = 0;
	hdmitx_edid_set_default_aud(hdmitx_device);
	rx_set_hdr_lumi(&tmp[0], 2);
	rx_set_receiver_edid(&tmp[0], 2);
}

/*
 * print one block data of edid
 */
#define TMP_EDID_BUF_SIZE	(256+8)
static void hdmitx_edid_blk_print(unsigned char *blk, unsigned int blk_idx)
{
	unsigned int i, pos;
	unsigned char *tmp_buf = NULL;

	tmp_buf = kmalloc(TMP_EDID_BUF_SIZE, GFP_KERNEL);
	if (!tmp_buf)
		return;

	memset(tmp_buf, 0, TMP_EDID_BUF_SIZE);
	pr_info(EDID "blk%d raw data\n", blk_idx);
	for (i = 0, pos = 0; i < 128; i++) {
		pos += sprintf(tmp_buf + pos, "%02x", blk[i]);
		if (((i+1) & 0x1f) == 0)    /* print 32bytes a line */
			pos += sprintf(tmp_buf + pos, "\n");
	}
	pos += sprintf(tmp_buf + pos, "\n");
	pr_info(EDID "\n%s\n", tmp_buf);
	kfree(tmp_buf);
}

/*
 * check EDID buf contains valid block numbers
 */
static unsigned int hdmitx_edid_check_valid_blocks(unsigned char *buf)
{
	unsigned int valid_blk_no = 0;
	unsigned int i = 0, j = 0;
	unsigned int tmp_chksum = 0;

	for (j = 0; j < EDID_MAX_BLOCK; j++) {
		for (i = 0; i < 128; i++)
			tmp_chksum += buf[i + j*128];
		if (tmp_chksum != 0) {
			valid_blk_no++;
			if (tmp_chksum & 0xff)
				pr_info(EDID "check sum invalid\n");
		}
		tmp_chksum = 0;
	}
	return valid_blk_no;
}

/*
 * suppose DDC read EDID two times successfully,
 * then compare EDID_buf and EDID_buf1.
 * if same, just print out EDID_buf raw data, else print out 2 buffers
 */
void hdmitx_edid_buf_compare_print(struct hdmitx_dev *hdmitx_device)
{
	unsigned int i = 0;
	unsigned int err_no = 0;
	unsigned char *buf0 = hdmitx_device->EDID_buf;
	unsigned char *buf1 = hdmitx_device->EDID_buf1;
	unsigned int valid_blk_no = 0;
	unsigned int blk_idx = 0;

	for (i = 0; i < EDID_MAX_BLOCK*128; i++) {
		if (buf0[i] != buf1[i])
			err_no++;
	}

	if (err_no == 0) {
		/* calculate valid edid block numbers */
		valid_blk_no = hdmitx_edid_check_valid_blocks(buf0);

		if (valid_blk_no == 0)
			pr_info(EDID "raw data are all zeroes\n");
		else {
			for (blk_idx = 0; blk_idx < valid_blk_no; blk_idx++)
				hdmitx_edid_blk_print(&buf0[blk_idx*128],
					blk_idx);
		}
	} else {
		pr_info(EDID "%d errors between two reading\n", err_no);
		valid_blk_no = hdmitx_edid_check_valid_blocks(buf0);
		for (blk_idx = 0; blk_idx < valid_blk_no; blk_idx++)
			hdmitx_edid_blk_print(&buf0[blk_idx*128], blk_idx);

		valid_blk_no = hdmitx_edid_check_valid_blocks(buf1);
		for (blk_idx = 0; blk_idx < valid_blk_no; blk_idx++)
			hdmitx_edid_blk_print(&buf1[blk_idx*128], blk_idx);
	}
}

int hdmitx_edid_dump(struct hdmitx_dev *hdmitx_device, char *buffer,
	int buffer_len)
{
	int i, pos = 0;
	struct rx_cap *prxcap = &hdmitx_device->rxcap;

	pos += snprintf(buffer+pos, buffer_len-pos,
		"Rx Manufacturer Name: %s\n", prxcap->IDManufacturerName);
	pos += snprintf(buffer+pos, buffer_len-pos,
		"Rx Product Code: %02x%02x\n",
		prxcap->IDProductCode[0],
		prxcap->IDProductCode[1]);
	pos += snprintf(buffer+pos, buffer_len-pos,
		"Rx Serial Number: %02x%02x%02x%02x\n",
		prxcap->IDSerialNumber[0],
		prxcap->IDSerialNumber[1],
		prxcap->IDSerialNumber[2],
		prxcap->IDSerialNumber[3]);
	pos += snprintf(buffer+pos, buffer_len-pos,
		"Rx Product Name: %s\n", prxcap->ReceiverProductName);

	pos += snprintf(buffer+pos, buffer_len-pos,
		"Manufacture Week: %d\n", prxcap->manufacture_week);
	pos += snprintf(buffer+pos, buffer_len-pos,
		"Manufacture Year: %d\n", prxcap->manufacture_year + 1990);

	pos += snprintf(buffer+pos, buffer_len-pos,
		"Physcial size(cm): %d x %d\n",
		prxcap->physcial_weight, prxcap->physcial_height);

	pos += snprintf(buffer+pos, buffer_len-pos,
		"EDID Version: %d.%d\n",
		prxcap->edid_version, prxcap->edid_revision);

	pos += snprintf(buffer+pos, buffer_len-pos,
		"EDID block number: 0x%x\n", hdmitx_device->EDID_buf[0x7e]);
	pos += snprintf(buffer+pos, buffer_len-pos,
		"blk0 chksum: 0x%02x\n", prxcap->blk0_chksum);

	pos += snprintf(buffer+pos, buffer_len-pos,
		"Source Physical Address[a.b.c.d]: %x.%x.%x.%x\n",
		hdmitx_device->hdmi_info.vsdb_phy_addr.a,
		hdmitx_device->hdmi_info.vsdb_phy_addr.b,
		hdmitx_device->hdmi_info.vsdb_phy_addr.c,
		hdmitx_device->hdmi_info.vsdb_phy_addr.d);

	pos += snprintf(buffer+pos, buffer_len-pos,
		"native Mode %x, VIC (native %d):\n",
		prxcap->native_Mode, prxcap->native_VIC);

	pos += snprintf(buffer+pos, buffer_len-pos,
		"ColorDeepSupport %x\n", prxcap->ColorDeepSupport);

	for (i = 0 ; i < prxcap->VIC_count ; i++) {
		pos += snprintf(buffer+pos, buffer_len-pos, "%d ",
		prxcap->VIC[i]);
	}
	pos += snprintf(buffer+pos, buffer_len-pos, "\n");
	pos += snprintf(buffer+pos, buffer_len-pos,
		"Audio {format, channel, freq, cce}\n");
	for (i = 0; i < prxcap->AUD_count; i++) {
		pos += snprintf(buffer+pos, buffer_len-pos,
			"{%d, %d, %x, %x}\n",
			prxcap->RxAudioCap[i].audio_format_code,
			prxcap->RxAudioCap[i].channel_num_max,
			prxcap->RxAudioCap[i].freq_cc,
			prxcap->RxAudioCap[i].cc3);
	}
	pos += snprintf(buffer+pos, buffer_len-pos,
		"Speaker Allocation: %x\n", prxcap->RxSpeakerAllocation);
	pos += snprintf(buffer+pos, buffer_len-pos,
		"Vendor: 0x%x ( %s device)\n",
		prxcap->ieeeoui, (prxcap->ieeeoui) ? "HDMI" : "DVI");

	pos += snprintf(buffer+pos, buffer_len-pos,
		"MaxTMDSClock1 %d MHz\n", prxcap->Max_TMDS_Clock1 * 5);

	if (prxcap->hf_ieeeoui) {
		pos += snprintf(buffer+pos, buffer_len-pos, "Vendor2: 0x%x\n",
			prxcap->hf_ieeeoui);
		pos += snprintf(buffer+pos, buffer_len-pos,
			"MaxTMDSClock2 %d MHz\n", prxcap->Max_TMDS_Clock2 * 5);
	}

	if (prxcap->allm)
		pos += snprintf(buffer+pos, buffer_len-pos, "ALLM: %x\n",
			prxcap->allm);

	pos += snprintf(buffer+pos, buffer_len-pos, "vLatency: ");
	if (prxcap->vLatency == LATENCY_INVALID_UNKNOWN)
		pos += snprintf(buffer+pos, buffer_len-pos,
				" Invalid/Unknown\n");
	else if (prxcap->vLatency == LATENCY_NOT_SUPPORT)
		pos += snprintf(buffer+pos, buffer_len-pos,
			" UnSupported\n");
	else
		pos += snprintf(buffer+pos, buffer_len-pos,
			" %d\n", prxcap->vLatency);

	pos += snprintf(buffer+pos, buffer_len-pos, "aLatency: ");
	if (prxcap->aLatency == LATENCY_INVALID_UNKNOWN)
		pos += snprintf(buffer+pos, buffer_len-pos,
				" Invalid/Unknown\n");
	else if (prxcap->aLatency == LATENCY_NOT_SUPPORT)
		pos += snprintf(buffer+pos, buffer_len-pos,
			" UnSupported\n");
	else
		pos += snprintf(buffer+pos, buffer_len-pos, " %d\n",
			prxcap->aLatency);

	pos += snprintf(buffer+pos, buffer_len-pos, "i_vLatency: ");
	if (prxcap->i_vLatency == LATENCY_INVALID_UNKNOWN)
		pos += snprintf(buffer+pos, buffer_len-pos,
				" Invalid/Unknown\n");
	else if (prxcap->i_vLatency == LATENCY_NOT_SUPPORT)
		pos += snprintf(buffer+pos, buffer_len-pos,
			" UnSupported\n");
	else
		pos += snprintf(buffer+pos, buffer_len-pos, " %d\n",
			prxcap->i_vLatency);

	pos += snprintf(buffer+pos, buffer_len-pos, "i_aLatency: ");
	if (prxcap->i_aLatency == LATENCY_INVALID_UNKNOWN)
		pos += snprintf(buffer+pos, buffer_len-pos,
				" Invalid/Unknown\n");
	else if (prxcap->i_aLatency == LATENCY_NOT_SUPPORT)
		pos += snprintf(buffer+pos, buffer_len-pos,
			" UnSupported\n");
	else
		pos += snprintf(buffer+pos, buffer_len-pos, " %d\n",
			prxcap->i_aLatency);

	if (prxcap->colorimetry_data)
		pos += snprintf(buffer+pos, buffer_len-pos,
			"ColorMetry: 0x%x\n", prxcap->colorimetry_data);
	pos += snprintf(buffer+pos, buffer_len-pos, "SCDC: %x\n",
		prxcap->scdc_present);
	pos += snprintf(buffer+pos, buffer_len-pos, "RR_Cap: %x\n",
		prxcap->scdc_rr_capable);
	pos += snprintf(buffer+pos, buffer_len-pos, "LTE_340M_Scramble: %x\n",
		prxcap->lte_340mcsc_scramble);

	if (prxcap->dv_info.ieeeoui == 0x00d046)
		pos += snprintf(buffer+pos, buffer_len-pos,
			"  DolbyVision%d", prxcap->dv_info.ver);
	if (prxcap->hdr_sup_eotf_smpte_st_2084)
		pos += snprintf(buffer+pos, buffer_len-pos, "  HDR");
	if (prxcap->dc_y444 || prxcap->dc_30bit || prxcap->dc_30bit_420)
		pos += snprintf(buffer+pos, buffer_len-pos, "  DeepColor");
	pos += snprintf(buffer+pos, buffer_len-pos, "\n");

	/* for checkvalue which maybe used by application to adjust
	 * whether edid is changed
	 */
	pos += snprintf(buffer+pos, buffer_len-pos,
		"checkvalue: 0x%02x%02x%02x%02x\n",
			edid_checkvalue[0],
			edid_checkvalue[1],
			edid_checkvalue[2],
			edid_checkvalue[3]);

	return pos;
}

bool hdmitx_check_edid_all_zeros(unsigned char *buf)
{
	unsigned int i = 0, j = 0;
	unsigned int chksum = 0;

	for (j = 0; j < EDID_MAX_BLOCK; j++) {
		chksum = 0;
		for (i = 0; i < 128; i++)
			chksum += buf[i + j * 128];
		if (chksum != 0)
			return false;
	}
	return true;
}

static bool hdmitx_edid_header_invalid(unsigned char *buf)
{
	bool base_blk_invalid = false;
	bool ext_blk_invalid = false;
	bool ret = false;
	int i = 0;

	if ((buf[0] != 0) || (buf[7] != 0)) {
		base_blk_invalid = true;
	} else {
		for (i = 1; i < 7; i++) {
			if (buf[i] != 0xff) {
				base_blk_invalid = true;
				break;
			}
		}
	}
	/* judge header strickly, only if both header invalid */
	if (buf[0x7e] > 0) {
		if ((buf[0x80] != 0x2) && (buf[0x80] != 0xf0))
			ext_blk_invalid = true;
		ret = base_blk_invalid && ext_blk_invalid;
	} else {
		ret = base_blk_invalid;
	}

	return ret;
}

bool hdmitx_edid_notify_ng(unsigned char *buf)
{
	if (!buf)
		return true;
	/* notify EDID NG to systemcontrol */
	if (hdmitx_check_edid_all_zeros(buf))
		return true;
	else if ((buf[0x7e] > 3) &&
		hdmitx_edid_header_invalid(buf))
		return true;
	/* may extend NG case here */

	return false;
}
