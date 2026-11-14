// SPDX-License-Identifier: GPL-2.0-only
/*
 * AW8697 haptic driver for Linux 6.19
 *
 * Based on OnePlus vendor aw8697.c, adapted for Linux 6.19.
 * Uses vendor firmware haptic_ram_0619_170.bin.
 *
 * KEY FIX: vendor aw8697_i2c_write_bits() has OPPOSITE mask semantics
 * compared to regmap_update_bits(). Vendor mask is "keep bits", regmap
 * mask is "modify bits". So all masks below use BIT()/GENMASK() of the
 * actual field, NOT the vendor AW8697_BIT_*_MASK macros.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/types.h>

/* ---- AW8697 register map ---- */
#define AW8697_REG_ID            0x00
#define AW8697_REG_SYSST         0x01
#define AW8697_REG_SYSINT        0x02
#define AW8697_REG_SYSINTM       0x03
#define AW8697_REG_SYSCTRL       0x04
#define AW8697_REG_GO            0x05
#define AW8697_REG_RTP_DATA      0x06
#define AW8697_REG_WAVSEQ1       0x07
#define AW8697_REG_WAVLOOP1      0x0f
#define AW8697_REG_BASE_ADDRH    0x21
#define AW8697_REG_BASE_ADDRL    0x22
#define AW8697_REG_FIFO_AEH      0x23
#define AW8697_REG_FIFO_AEL      0x24
#define AW8697_REG_FIFO_AFH      0x25
#define AW8697_REG_FIFO_AFL      0x26
#define AW8697_REG_DATCTRL       0x2b
#define AW8697_REG_PWMPRC        0x2d
#define AW8697_REG_PWMDBG        0x2e
#define AW8697_REG_DBGSTAT       0x30
#define AW8697_REG_BSTDBG1       0x31
#define AW8697_REG_BSTDBG2       0x32
#define AW8697_REG_BSTDBG3       0x33
#define AW8697_REG_BSTCFG        0x34
#define AW8697_REG_ANADBG        0x35
#define AW8697_REG_ANACTRL       0x36
#define AW8697_REG_DATDBG        0x39
#define AW8697_REG_BSTDBG4       0x3a
#define AW8697_REG_PRLVL         0x3e
#define AW8697_REG_PRTIME        0x3f
#define AW8697_REG_RAMADDRH      0x40
#define AW8697_REG_RAMADDRL      0x41
#define AW8697_REG_RAMDATA       0x42
#define AW8697_REG_GLB_STATE     0x46
#define AW8697_REG_BST_AUTO      0x47
#define AW8697_REG_CONT_CTRL     0x48
#define AW8697_REG_F_PRE_H       0x49
#define AW8697_REG_F_PRE_L       0x4a
#define AW8697_REG_TD_H          0x4b
#define AW8697_REG_TD_L          0x4c
#define AW8697_REG_TSET          0x4d
#define AW8697_REG_TRIM_LRA      0x5b
#define AW8697_REG_R_SPARE       0x5d
#define AW8697_REG_D2SCFG        0x5e
#define AW8697_REG_DETCTRL       0x5f
#define AW8697_REG_VBATDET       0x62
#define AW8697_REG_ADCTEST       0x66
#define AW8697_REG_F_LRA_F0_H    0x68
#define AW8697_REG_F_LRA_F0_L    0x69
#define AW8697_REG_WAIT_VOL_MP   0x6d
#define AW8697_REG_WAIT_VOL_MN   0x6f
#define AW8697_REG_ZC_THRSH_H    0x72
#define AW8697_REG_ZC_THRSH_L    0x73
#define AW8697_REG_BEMF_VTHH_H   0x74
#define AW8697_REG_BEMF_VTHH_L   0x75
#define AW8697_REG_BEMF_VTHL_H   0x76
#define AW8697_REG_BEMF_VTHL_L   0x77
#define AW8697_REG_BEMF_NUM      0x78
#define AW8697_REG_DRV_TIME      0x79
#define AW8697_REG_TIME_NZC      0x7a
#define AW8697_REG_DRV_LVL       0x7b
#define AW8697_REG_DRV_LVL_OV    0x7c
#define AW8697_REG_NUM_F0_1      0x7d
#define AW8697_REG_NUM_F0_2      0x7e
#define AW8697_REG_NUM_F0_3      0x7f

/* ---- 字段 mask（regmap 语义：要修改的位） ---- */
#define AW8697_MSK_SYSCTRL_WAVDAT       GENMASK(7, 6)
#define AW8697_MSK_SYSCTRL_RAMINIT      BIT(5)
#define AW8697_MSK_SYSCTRL_PLAY_MODE    GENMASK(3, 2)
#define AW8697_MSK_SYSCTRL_BST_MODE     BIT(1)
#define AW8697_MSK_SYSCTRL_WORK_MODE    BIT(0)

#define AW8697_VAL_SYSCTRL_PLAY_RAM     (0 << 2)
#define AW8697_VAL_SYSCTRL_PLAY_RTP     (1 << 2)
#define AW8697_VAL_SYSCTRL_PLAY_CONT    (2 << 2)
#define AW8697_VAL_SYSCTRL_BST_BOOST    BIT(1)
#define AW8697_VAL_SYSCTRL_BST_BYPASS   0
#define AW8697_VAL_SYSCTRL_STANDBY      BIT(0)
#define AW8697_VAL_SYSCTRL_ACTIVE       0

#define AW8697_MSK_GO                   BIT(0)
#define AW8697_VAL_GO_ENABLE            BIT(0)
#define AW8697_VAL_GO_DISABLE           0

#define AW8697_MSK_SYSINTM_UVLO         BIT(5)
#define AW8697_VAL_SYSINTM_UVLO_OFF     0
#define AW8697_VAL_SYSINTM_UVLO_EN      BIT(5)

#define AW8697_MSK_CONT_CTRL_EN_CLOSE   BIT(3)
#define AW8697_VAL_CONT_CTRL_CLOSE      BIT(3)
#define AW8697_VAL_CONT_CTRL_OPEN       0
#define AW8697_MSK_CONT_CTRL_F0_DETECT  BIT(2)
#define AW8697_VAL_CONT_CTRL_F0_EN      BIT(2)
#define AW8697_VAL_CONT_CTRL_F0_DIS     0
#define AW8697_MSK_CONT_CTRL_AUTO_BRK   BIT(0)
#define AW8697_VAL_CONT_CTRL_BRK_EN     BIT(0)

#define AW8697_MSK_ANACTRL_LRA_SRC      BIT(5)
#define AW8697_VAL_ANACTRL_LRA_SRC_REG  BIT(5)
#define AW8697_VAL_ANACTRL_LRA_SRC_EFUSE 0

#define AW8697_MSK_ANADBG_IOC           GENMASK(3, 2)
#define AW8697_VAL_ANADBG_IOC_4P65A     (3 << 2)

#define AW8697_MSK_DATCTRL_FC           GENMASK(7, 6)
#define AW8697_VAL_DATCTRL_FC_1000HZ    (3 << 6)
#define AW8697_MSK_DATCTRL_LPF          BIT(5)
#define AW8697_VAL_DATCTRL_LPF_EN       BIT(5)

#define AW8697_MSK_PWMDBG_PWM_MODE      GENMASK(6, 5)
#define AW8697_VAL_PWMDBG_PWM_24K       (2 << 5)

#define AW8697_MSK_BSTCFG_PEAKCUR       GENMASK(2, 0)
#define AW8697_VAL_BSTCFG_PEAKCUR_3P5A  (5 << 0)

#define AW8697_MSK_ADCTEST_VBAT_MODE    BIT(6)
#define AW8697_VAL_ADCTEST_VBAT_HW      BIT(6)
#define AW8697_VAL_ADCTEST_VBAT_SW      0

#define AW8697_MSK_BEMF_NUM_BRK         GENMASK(3, 0)

#define AW8697_MSK_PWMPRC_PRC           BIT(7)
#define AW8697_MSK_PRLVL_PR             BIT(7)
#define AW8697_MSK_DETCTRL_PROTECT      BIT(5)

#define AW8697_HAPTIC_F0_PRE            1700
#define AW8697_HAPTIC_F0_COEFF          260
#define AW8697_HAPTIC_F0_CALI_PERCEN    7
#define AW8697_HAPTIC_CONT_DRV_LVL      105
#define AW8697_HAPTIC_CONT_DRV_LVL_OV   125
#define AW8697_HAPTIC_CONT_TD           0x009a
#define AW8697_HAPTIC_CONT_ZC_THR       0x0ff1
#define AW8697_HAPTIC_CONT_NUM_BRK      3

static const u8 aw8697_firmware[] = {
  0xf2, 0x54, 0x08, 0x00, 0x85, 0x08, 0x11, 0x09, 0x8f, 0x09, 0x90, 0x0a,
  0xa0, 0x0a, 0xa1, 0x0b, 0x97, 0x0b, 0x98, 0x0c, 0x24, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03,
  0x03, 0x04, 0x05, 0x05, 0x06, 0x07, 0x08, 0x0a, 0x0b, 0x0d, 0x0f, 0x11,
  0x14, 0x16, 0x1a, 0x1e, 0x22, 0x27, 0x2d, 0x33, 0x3b, 0x43, 0x4c, 0x53,
  0x59, 0x5e, 0x63, 0x67, 0x6b, 0x6e, 0x70, 0x73, 0x75, 0x76, 0x78, 0x79,
  0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7e, 0x7e, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7e, 0x7e, 0x7d, 0x7d, 0x7c, 0x7b, 0x7a, 0x79, 0x78, 0x76,
  0x74, 0x72, 0x70, 0x6d, 0x6a, 0x67, 0x62, 0x5e, 0x58, 0x52, 0x4b, 0x43,
  0x39, 0x2f, 0x22, 0x14, 0x04, 0xf4, 0xe5, 0xd7, 0xcc, 0xc2, 0xb9, 0xb1,
  0xab, 0xa5, 0xa0, 0x9b, 0x98, 0x94, 0x91, 0x8f, 0x8c, 0x8b, 0x89, 0x87,
  0x86, 0x85, 0x84, 0x83, 0x83, 0x82, 0x82, 0x81, 0x81, 0x81, 0x81, 0x81,
  0x81, 0x81, 0x81, 0x81, 0x81, 0x82, 0x82, 0x83, 0x83, 0x84, 0x85, 0x86,
  0x87, 0x89, 0x8a, 0x8c, 0x8f, 0x91, 0x94, 0x97, 0x9b, 0xa0, 0xa5, 0xab,
  0xb1, 0xb9, 0xc2, 0xcc, 0xd7, 0xe4, 0xf3, 0x04, 0x14, 0x22, 0x2e, 0x39,
  0x42, 0x4a, 0x51, 0x57, 0x5d, 0x61, 0x65, 0x68, 0x6b, 0x6e, 0x70, 0x72,
  0x73, 0x74, 0x75, 0x75, 0x76, 0x76, 0x75, 0x75, 0x74, 0x73, 0x72, 0x70,
  0x6e, 0x6b, 0x68, 0x65, 0x61, 0x5c, 0x57, 0x51, 0x4a, 0x42, 0x39, 0x2e,
  0x21, 0x13, 0x03, 0xf5, 0xe8, 0xdd, 0xd3, 0xca, 0xc3, 0xbc, 0xb7, 0xb2,
  0xae, 0xab, 0xa8, 0xa6, 0xa5, 0xa3, 0xa3, 0xa2, 0xa2, 0xa3, 0xa4, 0xa5,
  0xa7, 0xaa, 0xad, 0xb1, 0xb5, 0xba, 0xc0, 0xc7, 0xcf, 0xd9, 0xe3, 0xf0,
  0xfe, 0x0d, 0x1a, 0x26, 0x30, 0x39, 0x41, 0x47, 0x4d, 0x52, 0x57, 0x5b,
  0x5e, 0x61, 0x64, 0x66, 0x68, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70,
  0x70, 0x70, 0x71, 0x71, 0x71, 0x71, 0x71, 0x71, 0x70, 0x70, 0x6f, 0x6f,
  0x6e, 0x6d, 0x6c, 0x6a, 0x69, 0x67, 0x65, 0x63, 0x60, 0x5d, 0x59, 0x55,
  0x51, 0x4b, 0x45, 0x3e, 0x36, 0x2c, 0x22, 0x15, 0x07, 0xfb, 0xf0, 0xe6,
  0xdd, 0xd5, 0xce, 0xc9, 0xc4, 0xbf, 0xbb, 0xb8, 0xb5, 0xb2, 0xb0, 0xae,
  0xad, 0xab, 0xaa, 0xa9, 0xa8, 0xa7, 0xa6, 0xa5, 0xa5, 0xa4, 0xa4, 0xa4,
  0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
  0xa3, 0xa4, 0xa4, 0xa4, 0xa5, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab,
  0xad, 0xae, 0xb0, 0xb3, 0xb5, 0xb8, 0xbc, 0xbf, 0xc4, 0xc9, 0xcf, 0xd5,
  0xdb, 0xe0, 0xe4, 0xe7, 0xeb, 0xed, 0xf0, 0xf2, 0xf4, 0xf5, 0xf7, 0xf8,
  0xf9, 0xfa, 0xfb, 0xfc, 0xfc, 0xfd, 0xfd, 0xfe, 0xfe, 0xfe, 0xff, 0xff,
  0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x08,
  0x09, 0x0b, 0x0e, 0x11, 0x14, 0x18, 0x1d, 0x23, 0x2a, 0x33, 0x3d, 0x48,
  0x51, 0x59, 0x5f, 0x65, 0x69, 0x6d, 0x70, 0x73, 0x75, 0x77, 0x78, 0x79,
  0x7a, 0x7b, 0x7c, 0x7c, 0x7d, 0x7d, 0x7d, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e,
  0x7d, 0x7d, 0x7d, 0x7c, 0x7b, 0x7b, 0x7a, 0x78, 0x77, 0x75, 0x73, 0x71,
  0x6e, 0x6a, 0x66, 0x61, 0x5a, 0x53, 0x4a, 0x3f, 0x32, 0x23, 0x10, 0xfb,
  0xe6, 0xd5, 0xc7, 0xbb, 0xb1, 0xa9, 0xa2, 0x9d, 0x98, 0x94, 0x91, 0x8e,
  0x8c, 0x8a, 0x88, 0x87, 0x86, 0x85, 0x84, 0x83, 0x83, 0x82, 0x82, 0x82,
  0x82, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x82,
  0x82, 0x82, 0x83, 0x83, 0x84, 0x84, 0x85, 0x86, 0x87, 0x89, 0x8b, 0x8d,
  0x8f, 0x92, 0x96, 0x9a, 0xa0, 0xa6, 0xae, 0xb7, 0xc1, 0xcf, 0xde, 0xf1,
  0x05, 0x15, 0x22, 0x2b, 0x32, 0x36, 0x38, 0x38, 0x35, 0x2f, 0x27, 0x1c,
  0x0d, 0xfc, 0xe7, 0xd6, 0xc8, 0xbc, 0xb2, 0xaa, 0xa3, 0x9d, 0x98, 0x94,
  0x91, 0x8e, 0x8c, 0x8a, 0x88, 0x87, 0x86, 0x85, 0x84, 0x83, 0x83, 0x82,
  0x82, 0x82, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
  0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x82,
  0x82, 0x82, 0x83, 0x83, 0x84, 0x85, 0x85, 0x87, 0x88, 0x89, 0x8b, 0x8d,
  0x90, 0x93, 0x97, 0x9c, 0xa1, 0xa8, 0xb0, 0xba, 0xc5, 0xd3, 0xe3, 0xf8,
  0x0c, 0x1e, 0x2d, 0x3a, 0x44, 0x4d, 0x54, 0x59, 0x5e, 0x62, 0x65, 0x67,
  0x69, 0x6a, 0x6a, 0x6a, 0x6a, 0x69, 0x67, 0x65, 0x62, 0x5e, 0x59, 0x53,
  0x4c, 0x43, 0x39, 0x2f, 0x27, 0x21, 0x1b, 0x17, 0x13, 0x10, 0x0d, 0x0b,
  0x09, 0x07, 0x06, 0x05, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x01,
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
  0xff, 0xfe, 0xfe, 0xfd, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf7, 0xf5, 0xf4,
  0xf2, 0xf1, 0xf1, 0xf0, 0xf0, 0xef, 0xef, 0xef, 0xf0, 0xf0, 0xf1, 0xf2,
  0xf3, 0xf4, 0xf6, 0xf8, 0xfb, 0xfe, 0x01, 0x06, 0x0b, 0x12, 0x1a, 0x24,
  0x2d, 0x33, 0x38, 0x3b, 0x3d, 0x3d, 0x3b, 0x38, 0x34, 0x2e, 0x26, 0x1b,
  0x0e, 0xff, 0xf1, 0xe4, 0xda, 0xd2, 0xcb, 0xc5, 0xc0, 0xbc, 0xb8, 0xb6,
  0xb3, 0xb1, 0xb0, 0xae, 0xad, 0xac, 0xab, 0xab, 0xaa, 0xaa, 0xa9, 0xa9,
  0xa9, 0xa9, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8, 0xa8,
  0xa8, 0xa8, 0xa8, 0xa9, 0xa9, 0xa9, 0xa9, 0xaa, 0xaa, 0xab, 0xac, 0xad,
  0xae, 0xaf, 0xb0, 0xb2, 0xb4, 0xb7, 0xba, 0xbd, 0xc2, 0xc7, 0xce, 0xd5,
  0xde, 0xe9, 0xf5, 0x00, 0x0c, 0x1a, 0x2a, 0x38, 0x43, 0x4c, 0x52, 0x57,
  0x5b, 0x5e, 0x60, 0x62, 0x63, 0x64, 0x65, 0x65, 0x66, 0x66, 0x65, 0x65,
  0x64, 0x62, 0x61, 0x5f, 0x5c, 0x59, 0x55, 0x51, 0x4b, 0x44, 0x3c, 0x33,
  0x27, 0x19, 0x08, 0xf4, 0xde, 0xcc, 0xbc, 0xb0, 0xa5, 0x9d, 0x95, 0x90,
  0x8b, 0x87, 0x85, 0x83, 0x81, 0x81, 0x81, 0x81, 0x82, 0x84, 0x87, 0x8b,
  0x8f, 0x95, 0x9c, 0xa4, 0xaf, 0xbb, 0xca, 0xdc, 0xf3, 0x09, 0x1d, 0x2e,
  0x3c, 0x47, 0x51, 0x59, 0x5f, 0x64, 0x69, 0x6c, 0x6f, 0x71, 0x72, 0x73,
  0x73, 0x73, 0x73, 0x71, 0x70, 0x6e, 0x6c, 0x6a, 0x68, 0x67, 0x65, 0x63,
  0x62, 0x60, 0x5d, 0x5b, 0x58, 0x54, 0x50, 0x4c, 0x46, 0x3f, 0x37, 0x2e,
  0x26, 0x20, 0x1a, 0x16, 0x12, 0x0f, 0x0c, 0x0a, 0x08, 0x07, 0x06, 0x05,
  0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
  0x00, 0x02, 0x04, 0x06, 0x08, 0x0b, 0x0d, 0x0f, 0x11, 0x13, 0x15, 0x17,
  0x19, 0x1b, 0x1d, 0x1e, 0x20, 0x22, 0x23, 0x25, 0x26, 0x28, 0x29, 0x2a,
  0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x30, 0x31, 0x31, 0x31, 0x31, 0x31,
  0x31, 0x31, 0x31, 0x31, 0x30, 0x30, 0x2f, 0x2f, 0x2e, 0x2d, 0x2c, 0x2b,
  0x2a, 0x28, 0x27, 0x26, 0x24, 0x23, 0x21, 0x1f, 0x1e, 0x1c, 0x1a, 0x18,
  0x16, 0x14, 0x12, 0x10, 0x0e, 0x0c, 0x0a, 0x07, 0x05, 0x03, 0x01, 0x00,
  0xfd, 0xfb, 0xf9, 0xf7, 0xf5, 0xf2, 0xf0, 0xee, 0xec, 0xea, 0xe8, 0xe6,
  0xe4, 0xe3, 0xe1, 0xdf, 0xde, 0xdc, 0xda, 0xd9, 0xd8, 0xd6, 0xd5, 0xd4,
  0xd3, 0xd2, 0xd2, 0xd1, 0xd0, 0xd0, 0xcf, 0xcf, 0xcf, 0xcf, 0xcf, 0xcf,
  0xcf, 0xcf, 0xcf, 0xd0, 0xd0, 0xd1, 0xd2, 0xd3, 0xd3, 0xd4, 0xd6, 0xd7,
  0xd8, 0xd9, 0xdb, 0xdc, 0xde, 0xe0, 0xe1, 0xe3, 0xe5, 0xe7, 0xe9, 0xeb,
  0xed, 0xef, 0xf1, 0xf3, 0xf5, 0xf7, 0xf9, 0xfc, 0xfe
};

struct aw8697_data {
    struct work_struct play_work;
    struct delayed_work stop_work;
    struct device *dev;
    struct input_dev *input_dev;
    struct i2c_client *client;
    struct regmap *regmap;
    struct gpio_desc *reset_gpio;
    bool running;
    bool ram_loaded;

    unsigned int f0;
    unsigned int f0_pre;
    unsigned int cont_td;
    unsigned int cont_zc_thr;
    unsigned char cont_drv_lvl;
    unsigned char cont_drv_lvl_ov;
    unsigned char cont_num_brk;
    unsigned char f0_cali_data;
};

static const struct regmap_config aw8697_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .cache_type = REGCACHE_NONE,
    .max_register = 0x7f,
};

/* ============================================================
 * Helpers
 * ============================================================ */

static int aw8697_wait_enter_standby(struct aw8697_data *haptics)
{
    unsigned int reg_val = 0;
    int err;

    err = regmap_read_poll_timeout(haptics->regmap, AW8697_REG_GLB_STATE, reg_val,
                                   ((reg_val & 0x0f) == 0x00),
                                   2000, 2000 * 200);
    if (err)
        dev_warn(haptics->dev, "did not enter standby: %d\n", err);
    return err;
}

static void aw8697_hw_reset(struct aw8697_data *haptics)
{
    /* reset-gpios 是 GPIO_ACTIVE_LOW */
    gpiod_set_value_cansleep(haptics->reset_gpio, 1);
    usleep_range(1000, 2000);
    gpiod_set_value_cansleep(haptics->reset_gpio, 0);
    usleep_range(3500, 4000);
}

static int aw8697_soft_reset(struct aw8697_data *haptics)
{
    int err;

    err = regmap_write(haptics->regmap, AW8697_REG_ID, 0xaa);
    if (err)
        return err;

    usleep_range(3000, 3500);
    return 0;
}

/* ============================================================
 * Play mode
 * ============================================================ */

static int aw8697_active(struct aw8697_data *haptics)
{
    unsigned int dummy;
    int err;

    err = regmap_update_bits(haptics->regmap, AW8697_REG_SYSCTRL,
                             AW8697_MSK_SYSCTRL_WORK_MODE,
                             AW8697_VAL_SYSCTRL_ACTIVE);
    if (err)
        return err;

    regmap_read(haptics->regmap, AW8697_REG_SYSINT, &dummy);

    err = regmap_update_bits(haptics->regmap, AW8697_REG_SYSINTM,
                             AW8697_MSK_SYSINTM_UVLO,
                             AW8697_VAL_SYSINTM_UVLO_EN);
    return err;
}

static int aw8697_play_mode(struct aw8697_data *haptics, unsigned char play_mode)
{
    int err;

    switch (play_mode) {
    case 0: /* STANDBY */
        err = regmap_update_bits(haptics->regmap, AW8697_REG_SYSINTM,
                                 AW8697_MSK_SYSINTM_UVLO,
                                 AW8697_VAL_SYSINTM_UVLO_OFF);
        if (err)
            return err;
        err = regmap_update_bits(haptics->regmap, AW8697_REG_SYSCTRL,
                                 AW8697_MSK_SYSCTRL_WORK_MODE,
                                 AW8697_VAL_SYSCTRL_STANDBY);
        break;

    case 1: /* RAM */
        err = regmap_update_bits(haptics->regmap, AW8697_REG_SYSCTRL,
                                 AW8697_MSK_SYSCTRL_PLAY_MODE,
                                 AW8697_VAL_SYSCTRL_PLAY_RAM);
        if (err)
            return err;
        err = regmap_update_bits(haptics->regmap, AW8697_REG_SYSCTRL,
                                 AW8697_MSK_SYSCTRL_BST_MODE,
                                 AW8697_VAL_SYSCTRL_BST_BYPASS);
        if (err)
            return err;
        err = aw8697_active(haptics);
        if (err)
            return err;
        err = regmap_update_bits(haptics->regmap, AW8697_REG_SYSCTRL,
                                 AW8697_MSK_SYSCTRL_BST_MODE,
                                 AW8697_VAL_SYSCTRL_BST_BOOST);
        if (err)
            return err;
        mdelay(2);
        break;

    case 4: /* CONT */
        err = regmap_update_bits(haptics->regmap, AW8697_REG_SYSCTRL,
                                 AW8697_MSK_SYSCTRL_PLAY_MODE,
                                 AW8697_VAL_SYSCTRL_PLAY_CONT);
        if (err)
            return err;
        err = regmap_update_bits(haptics->regmap, AW8697_REG_SYSCTRL,
                                 AW8697_MSK_SYSCTRL_BST_MODE,
                                 AW8697_VAL_SYSCTRL_BST_BYPASS);
        if (err)
            return err;
        err = aw8697_active(haptics);
        break;

    default:
        err = -EINVAL;
        break;
    }

    return err;
}

static int aw8697_play_go(struct aw8697_data *haptics, bool flag)
{
    unsigned int dummy;
    int err;

    err = regmap_update_bits(haptics->regmap, AW8697_REG_GO,
                             AW8697_MSK_GO,
                             flag ? AW8697_VAL_GO_ENABLE : AW8697_VAL_GO_DISABLE);
    if (err)
        return err;

    if (flag)
        mdelay(2);

    regmap_read(haptics->regmap, AW8697_REG_GLB_STATE, &dummy);
    return 0;
}

static int aw8697_stop(struct aw8697_data *haptics)
{
    int err;

    aw8697_play_go(haptics, false);
    aw8697_wait_enter_standby(haptics);
    err = aw8697_play_mode(haptics, 0);
    return err;
}

/* ============================================================
 * f0 calibration
 * ============================================================ */

static int aw8697_haptic_set_f0_preset(struct aw8697_data *haptics)
{
    unsigned int f0_reg;

    f0_reg = 1000000000 / (haptics->f0_pre * AW8697_HAPTIC_F0_COEFF);
    regmap_write(haptics->regmap, AW8697_REG_F_PRE_H, (f0_reg >> 8) & 0xff);
    regmap_write(haptics->regmap, AW8697_REG_F_PRE_L, f0_reg & 0xff);
    return 0;
}

static int aw8697_haptic_read_f0(struct aw8697_data *haptics)
{
    unsigned int reg_val = 0, f0_reg = 0;
    unsigned long f0_tmp;

    regmap_read(haptics->regmap, AW8697_REG_F_LRA_F0_H, &reg_val);
    f0_reg = (reg_val << 8);
    regmap_read(haptics->regmap, AW8697_REG_F_LRA_F0_L, &reg_val);
    f0_reg |= reg_val;

    if (f0_reg == 0) {
        dev_err(haptics->dev, "f0_reg = 0, f0 detect failed\n");
        haptics->f0 = 0;
        return -EIO;
    }

    f0_tmp = 1000000000 / (f0_reg * AW8697_HAPTIC_F0_COEFF);
    haptics->f0 = (unsigned int)f0_tmp;
    dev_info(haptics->dev, "lra f0 = %u\n", haptics->f0);
    return 0;
}

static int aw8697_haptic_get_f0(struct aw8697_data *haptics)
{
    unsigned int reg_val = 0;
    unsigned int i;
    unsigned int t_f0_ms, t_f0_trace_ms;
    unsigned char f0_pre_num = 0x05;
    unsigned char f0_wait_num = 0x03;
    unsigned char f0_repeat_num = 0x02;
    unsigned char f0_trace_num = 0x0f;
    int ret;

    dev_info(haptics->dev, "get_f0 enter\n");

    haptics->f0 = haptics->f0_pre;

    /* stop + TRIM_LRA = 0 */
    aw8697_stop(haptics);
    regmap_write(haptics->regmap, AW8697_REG_TRIM_LRA, 0);

    /* CONT 模式 */
    aw8697_play_mode(haptics, 4);

    /* CONT_CTRL: OPEN_PLAYBACK + F0_DETECT_ENABLE */
    regmap_update_bits(haptics->regmap, AW8697_REG_CONT_CTRL,
                       AW8697_MSK_CONT_CTRL_EN_CLOSE,
                       AW8697_VAL_CONT_CTRL_OPEN);
    regmap_update_bits(haptics->regmap, AW8697_REG_CONT_CTRL,
                       AW8697_MSK_CONT_CTRL_F0_DETECT,
                       AW8697_VAL_CONT_CTRL_F0_EN);

    /* LPF */
    regmap_update_bits(haptics->regmap, AW8697_REG_DATCTRL,
                       AW8697_MSK_DATCTRL_FC, AW8697_VAL_DATCTRL_FC_1000HZ);
    regmap_update_bits(haptics->regmap, AW8697_REG_DATCTRL,
                       AW8697_MSK_DATCTRL_LPF, AW8697_VAL_DATCTRL_LPF_EN);

    /* ANACTRL.LRA_SRC = REG */
    regmap_update_bits(haptics->regmap, AW8697_REG_ANACTRL,
                       AW8697_MSK_ANACTRL_LRA_SRC,
                       AW8697_VAL_ANACTRL_LRA_SRC_REG);

    /* f0 preset */
    aw8697_haptic_set_f0_preset(haptics);

    /* BEMF */
    regmap_write(haptics->regmap, AW8697_REG_BEMF_VTHH_H, 0x10);
    regmap_write(haptics->regmap, AW8697_REG_BEMF_VTHH_L, 0x08);
    regmap_write(haptics->regmap, AW8697_REG_BEMF_VTHL_H, 0x03);
    regmap_write(haptics->regmap, AW8697_REG_BEMF_VTHL_L, 0xf8);

    /* 驱动电平 */
    regmap_write(haptics->regmap, AW8697_REG_DRV_LVL, haptics->cont_drv_lvl);

    /* f0 trace parameter */
    regmap_write(haptics->regmap, AW8697_REG_NUM_F0_1, (f0_pre_num << 4) | f0_wait_num);
    regmap_write(haptics->regmap, AW8697_REG_NUM_F0_2, f0_repeat_num);
    regmap_write(haptics->regmap, AW8697_REG_NUM_F0_3, f0_trace_num);

    /* 清中断 */
    regmap_read(haptics->regmap, AW8697_REG_SYSINT, &reg_val);

    /* 诊断 */
    regmap_read(haptics->regmap, AW8697_REG_SYSCTRL, &reg_val);
    dev_info(haptics->dev, "pre-GO: SYSCTRL=0x%02x\n", reg_val);
    regmap_read(haptics->regmap, AW8697_REG_ANACTRL, &reg_val);
    dev_info(haptics->dev, "pre-GO: ANACTRL=0x%02x\n", reg_val);
    regmap_read(haptics->regmap, AW8697_REG_CONT_CTRL, &reg_val);
    dev_info(haptics->dev, "pre-GO: CONT_CTRL=0x%02x\n", reg_val);
    regmap_read(haptics->regmap, AW8697_REG_DATCTRL, &reg_val);
    dev_info(haptics->dev, "pre-GO: DATCTRL=0x%02x\n", reg_val);
    regmap_read(haptics->regmap, AW8697_REG_GLB_STATE, &reg_val);
    dev_info(haptics->dev, "pre-GO: GLB_STATE=0x%02x\n", reg_val);

    /* GO */
    aw8697_play_go(haptics, true);

    /* 等待 */
    t_f0_ms = 1000 * 10 / haptics->f0_pre;
    t_f0_trace_ms = t_f0_ms * (f0_pre_num + f0_wait_num +
                               (f0_trace_num + f0_wait_num) * (f0_repeat_num - 1));
    dev_info(haptics->dev, "t_f0_trace_ms=%u\n", t_f0_trace_ms);
    msleep(t_f0_trace_ms);

    /* 轮询 GLB_STATE */
    for (i = 0; i < 50; i++) {
        regmap_read(haptics->regmap, AW8697_REG_GLB_STATE, &reg_val);
        if ((reg_val & 0x0f) == 0x00) {
            ret = aw8697_haptic_read_f0(haptics);
            break;
        }
        msleep(10);
    }
    if (i == 50) {
        dev_err(haptics->dev, "wait standby timeout, GLB=0x%02x\n", reg_val);
        ret = -ETIMEDOUT;
    }

    /* 恢复默认 */
    regmap_update_bits(haptics->regmap, AW8697_REG_CONT_CTRL,
                       AW8697_MSK_CONT_CTRL_EN_CLOSE,
                       AW8697_VAL_CONT_CTRL_CLOSE);
    regmap_update_bits(haptics->regmap, AW8697_REG_CONT_CTRL,
                       AW8697_MSK_CONT_CTRL_F0_DETECT,
                       AW8697_VAL_CONT_CTRL_F0_DIS);

    return ret;
}

static int aw8697_haptic_f0_calibration(struct aw8697_data *haptics)
{
    unsigned int reg_val = 0;
    unsigned int f0_limit = 0;
    char f0_cali_lra;
    int f0_cali_step;
    int f0_dft_step;
    int ret = 0;

    dev_info(haptics->dev, "f0_calibration enter\n");

    regmap_write(haptics->regmap, AW8697_REG_TRIM_LRA, 0);

    ret = aw8697_haptic_get_f0(haptics);
    if (ret) {
        dev_err(haptics->dev, "get f0 error\n");
        return ret;
    }

    f0_limit = haptics->f0;
    if (haptics->f0 * 100 < AW8697_HAPTIC_F0_PRE * (100 - AW8697_HAPTIC_F0_CALI_PERCEN))
        f0_limit = AW8697_HAPTIC_F0_PRE * (100 - AW8697_HAPTIC_F0_CALI_PERCEN) / 100;
    if (haptics->f0 * 100 > AW8697_HAPTIC_F0_PRE * (100 + AW8697_HAPTIC_F0_CALI_PERCEN))
        f0_limit = AW8697_HAPTIC_F0_PRE * (100 + AW8697_HAPTIC_F0_CALI_PERCEN) / 100;

    f0_cali_step = 10000 * ((int)f0_limit - (int)haptics->f0_pre) / ((int)f0_limit * 25);
    dev_info(haptics->dev, "f0_cali_step=%d\n", f0_cali_step);

    regmap_read(haptics->regmap, AW8697_REG_TRIM_LRA, &reg_val);
    if (reg_val & 0x20)
        f0_dft_step = reg_val - 0x40;
    else
        f0_dft_step = reg_val;

    f0_cali_step += f0_dft_step;

    if (f0_cali_step > 31)
        f0_cali_step = 31;
    else if (f0_cali_step < -32)
        f0_cali_step = -32;

    f0_cali_lra = (char)f0_cali_step;
    if (f0_cali_step < 0)
        f0_cali_lra += 0x40;

    regmap_write(haptics->regmap, AW8697_REG_TRIM_LRA, (u8)f0_cali_lra);
    regmap_read(haptics->regmap, AW8697_REG_TRIM_LRA, &reg_val);
    haptics->f0_cali_data = reg_val;
    dev_info(haptics->dev, "final trim_lra=0x%02x\n", reg_val);

    aw8697_play_mode(haptics, 0);
    aw8697_stop(haptics);

    return ret;
}

/* ============================================================
 * Firmware upload
 * ============================================================ */

static int aw8697_upload_waveform(struct aw8697_data *haptics)
{
    const u8 *ram_data;
    size_t ram_len;
    int ret;

    dev_info(haptics->dev, "embedded fw size=%zu base=0x%02x%02x\n",
             sizeof(aw8697_firmware),
             aw8697_firmware[2], aw8697_firmware[3]);

    /* RAMINIT on */
    ret = regmap_update_bits(haptics->regmap, AW8697_REG_SYSCTRL,
                             AW8697_MSK_SYSCTRL_RAMINIT, BIT(5));
    if (ret)
        return ret;

    /* base addr */
    regmap_write(haptics->regmap, AW8697_REG_BASE_ADDRH, aw8697_firmware[2]);
    regmap_write(haptics->regmap, AW8697_REG_BASE_ADDRL, aw8697_firmware[3]);

    /* FIFO 阈值 */
    {
        unsigned int base_addr = (aw8697_firmware[2] << 8) | aw8697_firmware[3];
        regmap_write(haptics->regmap, AW8697_REG_FIFO_AEH, (base_addr >> 1) >> 8);
        regmap_write(haptics->regmap, AW8697_REG_FIFO_AEL, (base_addr >> 1) & 0xff);
        regmap_write(haptics->regmap, AW8697_REG_FIFO_AFH,
                     (base_addr - (base_addr >> 2)) >> 8);
        regmap_write(haptics->regmap, AW8697_REG_FIFO_AFL,
                     (base_addr - (base_addr >> 2)) & 0xff);
    }

    /* RAM addr */
    regmap_write(haptics->regmap, AW8697_REG_RAMADDRH, aw8697_firmware[2]);
    regmap_write(haptics->regmap, AW8697_REG_RAMADDRL, aw8697_firmware[3]);

    /* 写波形 */
    ram_data = aw8697_firmware + 4;
    ram_len = sizeof(aw8697_firmware) - 4;

    ret = regmap_noinc_write(haptics->regmap, AW8697_REG_RAMDATA, ram_data, ram_len);
    if (ret) {
        dev_err(haptics->dev, "write waveform failed: %d\n", ret);
        return ret;
    }

    dev_info(haptics->dev, "waveform uploaded: %zu bytes\n", ram_len);

    /* RAMINIT off */
    regmap_update_bits(haptics->regmap, AW8697_REG_SYSCTRL,
                       AW8697_MSK_SYSCTRL_RAMINIT, 0);

    haptics->ram_loaded = true;
    return 0;
}

/* ============================================================
 * Playback
 * ============================================================ */

static int aw8697_play_sine(struct aw8697_data *haptics)
{
    int err;

    err = aw8697_stop(haptics);
    if (err)
        return err;

    err = aw8697_play_mode(haptics, 1);
    if (err)
        return err;

    regmap_write(haptics->regmap, AW8697_REG_WAVSEQ1, 0x01);
    regmap_write(haptics->regmap, AW8697_REG_WAVLOOP1, 0x0f);

    regmap_write(haptics->regmap, AW8697_REG_DATDBG, 0x80);
    regmap_write(haptics->regmap, AW8697_REG_DRV_LVL, haptics->cont_drv_lvl);
    regmap_write(haptics->regmap, AW8697_REG_DRV_LVL_OV, haptics->cont_drv_lvl_ov);

    err = aw8697_play_go(haptics, true);
    return err;
}

static int aw8697_haptics_play(struct input_dev *dev, void *data,
                               struct ff_effect *effect)
{
    struct aw8697_data *haptics = input_get_drvdata(dev);
    int level;
    unsigned int duration;

    level = effect->u.rumble.strong_magnitude;
    if (!level)
        level = effect->u.rumble.weak_magnitude;

    if (!level) {
        haptics->running = 0;
        cancel_delayed_work(&haptics->stop_work);
        schedule_work(&haptics->play_work);
        return 0;
    }

    haptics->running = level;
    schedule_work(&haptics->play_work);

    duration = effect->replay.length;
    if (!duration)
        duration = 200;

    cancel_delayed_work(&haptics->stop_work);
    schedule_delayed_work(&haptics->stop_work, msecs_to_jiffies(duration));

    return 0;
}

static void aw8697_stop_work(struct work_struct *work)
{
    struct aw8697_data *haptics =
        container_of(to_delayed_work(work), struct aw8697_data, stop_work);

    haptics->running = 0;
    cancel_work_sync(&haptics->play_work);
    aw8697_stop(haptics);
}

static void aw8697_close(struct input_dev *input)
{
    struct aw8697_data *haptics = input_get_drvdata(input);

    cancel_delayed_work_sync(&haptics->stop_work); 
    cancel_work_sync(&haptics->play_work);
    aw8697_stop(haptics);
}

static void aw8697_play_work(struct work_struct *work)
{
    struct aw8697_data *haptics = container_of(work, struct aw8697_data, play_work);
    int err;

    if (haptics->running)
        err = aw8697_play_sine(haptics);
    else
        err = aw8697_stop(haptics);

    if (err)
        dev_err(haptics->dev, "play_work failed: %d\n", err);
}

/* ============================================================
 * Probe
 * ============================================================ */

static int aw8697_detect(struct aw8697_data *haptics)
{
    unsigned int chip_id = 0;
    int err;

    err = regmap_read(haptics->regmap, AW8697_REG_ID, &chip_id);
    if (err)
        return dev_err_probe(haptics->dev, err, "read CHIPID failed\n");

    if (chip_id != 0x97) {
        dev_err(haptics->dev, "unexpected CHIPID 0x%x\n", chip_id);
        return -ENODEV;
    }

    dev_info(haptics->dev, "AW8697 chip detected\n");
    return 0;
}

static int aw8697_probe(struct i2c_client *client)
{
    struct aw8697_data *haptics;
    int err;

    haptics = devm_kzalloc(&client->dev, sizeof(*haptics), GFP_KERNEL);
    if (!haptics)
        return -ENOMEM;

    haptics->dev = &client->dev;
    haptics->client = client;
    i2c_set_clientdata(client, haptics);

    haptics->f0_pre = AW8697_HAPTIC_F0_PRE;
    haptics->cont_td = AW8697_HAPTIC_CONT_TD;
    haptics->cont_zc_thr = AW8697_HAPTIC_CONT_ZC_THR;
    haptics->cont_drv_lvl = AW8697_HAPTIC_CONT_DRV_LVL;
    haptics->cont_drv_lvl_ov = AW8697_HAPTIC_CONT_DRV_LVL_OV;
    haptics->cont_num_brk = AW8697_HAPTIC_CONT_NUM_BRK;

    haptics->regmap = devm_regmap_init_i2c(client, &aw8697_regmap_config);
    if (IS_ERR(haptics->regmap))
        return dev_err_probe(haptics->dev, PTR_ERR(haptics->regmap),
                             "regmap init failed\n");

    haptics->input_dev = devm_input_allocate_device(haptics->dev);
    if (!haptics->input_dev)
        return -ENOMEM;

    haptics->reset_gpio = devm_gpiod_get(haptics->dev, "reset", GPIOD_OUT_HIGH);
    if (IS_ERR(haptics->reset_gpio))
        return dev_err_probe(haptics->dev, PTR_ERR(haptics->reset_gpio),
                             "get reset gpio failed\n");

    aw8697_hw_reset(haptics);

    err = aw8697_soft_reset(haptics);
    if (err)
        return dev_err_probe(haptics->dev, err, "soft reset failed\n");

    err = aw8697_detect(haptics);
    if (err)
        return err;

    regmap_write(haptics->regmap, AW8697_REG_BSTDBG1, 0x30);
    regmap_write(haptics->regmap, AW8697_REG_BSTDBG2, 0xeb);
    regmap_write(haptics->regmap, AW8697_REG_BSTDBG3, 0xd4);
    regmap_write(haptics->regmap, AW8697_REG_TSET, 0x12);
    regmap_write(haptics->regmap, AW8697_REG_R_SPARE, 0x68);
    regmap_update_bits(haptics->regmap, AW8697_REG_ANADBG,
                       AW8697_MSK_ANADBG_IOC, AW8697_VAL_ANADBG_IOC_4P65A);

    regmap_update_bits(haptics->regmap, AW8697_REG_PWMDBG,
                       AW8697_MSK_PWMDBG_PWM_MODE, AW8697_VAL_PWMDBG_PWM_24K);

    regmap_update_bits(haptics->regmap, AW8697_REG_BSTCFG,
                       AW8697_MSK_BSTCFG_PEAKCUR, AW8697_VAL_BSTCFG_PEAKCUR_3P5A);

    regmap_write(haptics->regmap, AW8697_REG_BST_AUTO, 0x00);

    regmap_update_bits(haptics->regmap, AW8697_REG_PWMPRC,
                       AW8697_MSK_PWMPRC_PRC, 0);
    regmap_update_bits(haptics->regmap, AW8697_REG_PRLVL,
                       AW8697_MSK_PRLVL_PR, 0);

    regmap_update_bits(haptics->regmap, AW8697_REG_ADCTEST,
                       AW8697_MSK_ADCTEST_VBAT_MODE, AW8697_VAL_ADCTEST_VBAT_HW);

    regmap_write(haptics->regmap, AW8697_REG_TD_H, (haptics->cont_td >> 8) & 0xff);
    regmap_write(haptics->regmap, AW8697_REG_TD_L, haptics->cont_td & 0xff);
    regmap_write(haptics->regmap, AW8697_REG_TSET, 0x12);
    regmap_write(haptics->regmap, AW8697_REG_ZC_THRSH_H, (haptics->cont_zc_thr >> 8) & 0xff);
    regmap_write(haptics->regmap, AW8697_REG_ZC_THRSH_L, haptics->cont_zc_thr & 0xff);
    regmap_update_bits(haptics->regmap, AW8697_REG_BEMF_NUM,
                       AW8697_MSK_BEMF_NUM_BRK, haptics->cont_num_brk);
    regmap_write(haptics->regmap, AW8697_REG_TIME_NZC, 0x23);

    err = aw8697_haptic_f0_calibration(haptics);
    if (err)
        dev_warn(haptics->dev, "f0 calibration failed: %d\n", err);

    err = aw8697_upload_waveform(haptics);
    if (err)
        return dev_err_probe(haptics->dev, err, "ram init failed\n");

    INIT_WORK(&haptics->play_work, aw8697_play_work);
    INIT_DELAYED_WORK(&haptics->stop_work, aw8697_stop_work);   /* ← 新增 */
    haptics->input_dev->name = "aw8697-haptics";
    haptics->input_dev->close = aw8697_close;
    input_set_drvdata(haptics->input_dev, haptics);
    input_set_capability(haptics->input_dev, EV_FF, FF_RUMBLE);

    err = input_ff_create_memless(haptics->input_dev, NULL, aw8697_haptics_play);
    if (err)
        return dev_err_probe(haptics->dev, err, "FF create failed\n");

    err = input_register_device(haptics->input_dev);
    if (err)
        return dev_err_probe(haptics->dev, err, "input register failed\n");

    dev_info(haptics->dev, "AW8697 haptic driver ready\n");
    return 0;
}

static const struct of_device_id aw8697_of_id[] = {
    { .compatible = "awinic,aw8697_haptic" },
    { }
};
MODULE_DEVICE_TABLE(of, aw8697_of_id);

static struct i2c_driver aw8697_driver = {
    .driver = {
        .name = "aw8697-haptics",
        .of_match_table = aw8697_of_id,
    },
    .probe = aw8697_probe,
};
module_i2c_driver(aw8697_driver);

MODULE_AUTHOR("OnePlus / AWINIC");
MODULE_DESCRIPTION("AWINIC AW8697 LRA Haptic Driver");
MODULE_LICENSE("GPL");
