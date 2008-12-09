/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2008 by Michael Sevakis
 *
 * Driver for WM8978 audio codec 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include "config.h"
#include "system.h"
#include "audiohw.h"
#include "wmcodec.h"
#include "audio.h"
//#define LOGF_ENABLE
#include "logf.h"

/* #define to help adjust lower volume limit */
#define HW_VOL_MIN                0
#define HW_VOL_MUTE               0
#define HW_VOL_MAX               96
#define HW_VOL_ANA_MIN            0
#define HW_VOL_ANA_MAX           63
#define HW_VOL_DIG_MAX          255
#define HW_VOL_DIG_THRESHOLD    (HW_VOL_MAX - HW_VOL_ANA_MAX)
#define HW_VOL_DIG_MIN          (HW_VOL_DIG_MAX - 2*HW_VOL_DIG_THRESHOLD)

/* TODO: Define/refine an API for special hardware steps outside the
 * main codec driver such as special GPIO handling. */
extern void audiohw_enable_headphone_jack(bool enable);

const struct sound_settings_info audiohw_settings[] =
{
    [SOUND_VOLUME]        = {"dB", 0,  1, -90,   6, -25},
    [SOUND_BASS]          = {"dB", 0,  1, -12,  12,   0},
    [SOUND_TREBLE]        = {"dB", 0,  1, -12,  12,   0},
    [SOUND_BALANCE]       = {"%",  0,  1,-100, 100,   0},
    [SOUND_CHANNELS]      = {"",   0,  1,   0,   5,   0},
    [SOUND_STEREO_WIDTH]  = {"%",  0,  5,   0, 250, 100},
#ifdef HAVE_RECORDING
    [SOUND_LEFT_GAIN]     = {"dB", 1,  1,-128,  96,   0},
    [SOUND_RIGHT_GAIN]    = {"dB", 1,  1,-128,  96,   0},
#if 0
    [SOUND_MIC_GAIN]      = {"dB", 1,  1,-128, 108,  16},
#endif
#endif
#if 0
    [SOUND_BASS_CUTOFF]   = {"",   0,  1,   1,   4,   1},
    [SOUND_TREBLE_CUTOFF] = {"",   0,  1,   1,   4,   1},
#endif
};

static uint16_t wmc_regs[WMC_NUM_REGISTERS] =
{
    /* Initialized with post-reset default values - the 2-wire interface
     * cannot be read. Or-in additional bits desired for some registers. */
    [0 ... WMC_NUM_REGISTERS-1]   = 0x8000, /* To ID invalids in gaps */
    [WMC_SOFTWARE_RESET]          = 0x000,
    [WMC_POWER_MANAGEMENT1]       = 0x000,
    [WMC_POWER_MANAGEMENT2]       = 0x000,
    [WMC_POWER_MANAGEMENT3]       = 0x000,
    [WMC_AUDIO_INTERFACE]         = 0x050,
    [WMC_COMPANDING_CTRL]         = 0x000,
    [WMC_CLOCK_GEN_CTRL]          = 0x140,
    [WMC_ADDITIONAL_CTRL]         = 0x000,
    [WMC_GPIO]                    = 0x000,
    [WMC_JACK_DETECT_CONTROL1]    = 0x000,
    [WMC_DAC_CONTROL]             = 0x000,
    [WMC_LEFT_DAC_DIGITAL_VOL]    = 0x0ff | WMC_VU,
    [WMC_RIGHT_DAC_DIGITAL_VOL]   = 0x0ff | WMC_VU,
    [WMC_JACK_DETECT_CONTROL2]    = 0x000,
    [WMC_ADC_CONTROL]             = 0x100,
    [WMC_LEFT_ADC_DIGITAL_VOL]    = 0x0ff | WMC_VU,
    [WMC_RIGHT_ADC_DIGITAL_VOL]   = 0x0ff | WMC_VU,
    [WMC_EQ1_LOW_SHELF]           = 0x12c,
    [WMC_EQ2_PEAK1]               = 0x02c,
    [WMC_EQ3_PEAK2]               = 0x02c,
    [WMC_EQ4_PEAK3]               = 0x02c,
    [WMC_EQ5_HIGH_SHELF]          = 0x02c,
    [WMC_DAC_LIMITER1]            = 0x032,
    [WMC_DAC_LIMITER2]            = 0x000,
    [WMC_NOTCH_FILTER1]           = 0x000,
    [WMC_NOTCH_FILTER2]           = 0x000,
    [WMC_NOTCH_FILTER3]           = 0x000,
    [WMC_NOTCH_FILTER4]           = 0x000,
    [WMC_ALC_CONTROL1]            = 0x038,
    [WMC_ALC_CONTROL2]            = 0x00b,
    [WMC_ALC_CONTROL3]            = 0x032,
    [WMC_NOISE_GATE]              = 0x000,
    [WMC_PLL_N]                   = 0x008,
    [WMC_PLL_K1]                  = 0x00c,
    [WMC_PLL_K2]                  = 0x093,
    [WMC_PLL_K3]                  = 0x0e9,
    [WMC_3D_CONTROL]              = 0x000,
    [WMC_BEEP_CONTROL]            = 0x000,
    [WMC_INPUT_CTRL]              = 0x033,
    [WMC_LEFT_INP_PGA_GAIN_CTRL]  = 0x010,
    [WMC_RIGHT_INP_PGA_GAIN_CTRL] = 0x010,
    [WMC_LEFT_ADC_BOOST_CTRL]     = 0x100,
    [WMC_RIGHT_ADC_BOOST_CTRL]    = 0x100,
    [WMC_OUTPUT_CTRL]             = 0x002,
    [WMC_LEFT_MIXER_CTRL]         = 0x001,
    [WMC_RIGHT_MIXER_CTRL]        = 0x001,
    [WMC_LOUT1_HP_VOLUME_CTRL]    = 0x039 | WMC_VU | WMC_ZC,
    [WMC_ROUT1_HP_VOLUME_CTRL]    = 0x039 | WMC_VU | WMC_ZC,
    [WMC_LOUT2_SPK_VOLUME_CTRL]   = 0x039 | WMC_VU | WMC_ZC,
    [WMC_ROUT2_SPK_VOLUME_CTRL]   = 0x039 | WMC_VU | WMC_ZC,
    [WMC_OUT3_MIXER_CTRL]         = 0x001,
    [WMC_OUT4_MONO_MIXER_CTRL]    = 0x001,
};

struct
{
    int vol_l;
    int vol_r;
    bool ahw_mute;
} wmc_vol =
{
    HW_VOL_MUTE, HW_VOL_MUTE, false
};

static void wmc_write(unsigned int reg, unsigned int val)
{
    if (reg >= WMC_NUM_REGISTERS || (wmc_regs[reg] & 0x8000))
    {
        logf("wm8978 invalid register: %d", reg);
        return;
    }

    wmc_regs[reg] = val & ~0x8000;
    wmcodec_write(reg, val);
}

void wmc_set(unsigned int reg, unsigned int bits)
{
    wmc_write(reg, wmc_regs[reg] | bits);
}

void wmc_clear(unsigned int reg, unsigned int bits)
{
    wmc_write(reg, wmc_regs[reg] & ~bits);
}

static void wmc_write_masked(unsigned int reg, unsigned int bits,
                             unsigned int mask)
{
    wmc_write(reg, (wmc_regs[reg] & ~mask) | (bits & mask));
}

/* convert tenth of dB volume (-890..60) to master volume register value
 * (000000...111111) */
int tenthdb2master(int db)
{
    /* -90dB to +6dB 1dB steps (96 levels) 7bits */
    /* 1100000 ==  +6dB  (0x60,96)               */
    /* 1101010 ==   0dB  (0x5a,90)               */
    /* 1000001 == -57dB  (0x21,33,DAC)           */
    /* 0000001 == -89dB  (0x01,01)               */
    /* 0000000 == -90dB  (0x00,00,Mute)          */
    if (db <= VOLUME_MIN)
    {
        return 0x0;
    }
    else
    {
        return (db - VOLUME_MIN) / 10;
    }
}

void audiohw_preinit(void)
{
    /* 1. Turn on external power supplies. Wait for supply voltage to settle. */

    /* Step 1 should be completed already. Reset and return all registers to
     * defaults */
    wmcodec_write(WMC_SOFTWARE_RESET, 0xff);
    sleep(HZ/10);

    /* 2. Mute all analogue outputs */
    wmc_set(WMC_LOUT1_HP_VOLUME_CTRL, WMC_MUTE | HW_VOL_ANA_MIN);
    wmc_set(WMC_ROUT1_HP_VOLUME_CTRL, WMC_MUTE | HW_VOL_ANA_MIN);
    wmc_set(WMC_LOUT2_SPK_VOLUME_CTRL, WMC_MUTE);
    wmc_set(WMC_ROUT2_SPK_VOLUME_CTRL, WMC_MUTE);
    wmc_set(WMC_OUT3_MIXER_CTRL, WMC_MUTE);
    wmc_set(WMC_OUT4_MONO_MIXER_CTRL, WMC_MUTE);
    wmc_set(WMC_INPUT_CTRL, 0x000);

    /* 3. Set L/RMIXEN = 1 and DACENL/R = 1 in register R3. */
    wmc_write(WMC_POWER_MANAGEMENT3,
              WMC_RMIXEN | WMC_LMIXEN | WMC_DACENR | WMC_DACENL);

    /* 4. Set BUFIOEN = 1 and VMIDSEL[1:0] to required value in register
     *    R1. Wait for VMID supply to settle */
    wmc_write(WMC_POWER_MANAGEMENT1, WMC_BUFIOEN | WMC_VMIDSEL_300K);
    sleep(HZ/10);

    /* 5. Set BIASEN = 1 in register R1. */
    wmc_set(WMC_POWER_MANAGEMENT1, WMC_BIASEN);
}

void audiohw_postinit(void)
{
    sleep(HZ);

    /* 6. Set L/ROUTEN = 1 in register R2. */
    wmc_write(WMC_POWER_MANAGEMENT2, WMC_LOUT1EN | WMC_ROUT1EN);

    /* 7. Enable other mixers as required */

    /* 8. Enable other outputs as required */

    /* 9. Set remaining registers */
    wmc_write(WMC_AUDIO_INTERFACE, WMC_WL_16 | WMC_FMT_I2S);
    wmc_write(WMC_DAC_CONTROL, WMC_DACOSR_128 | WMC_AMUTE);

    wmc_set(WMC_INPUT_CTRL, WMC_R2_2INPPGA | WMC_L2_2INPPGA);
    wmc_set(WMC_LEFT_INP_PGA_GAIN_CTRL, 0x3f);
    wmc_set(WMC_RIGHT_INP_PGA_GAIN_CTRL, 0x3f);
    wmc_set(WMC_LEFT_INP_PGA_GAIN_CTRL, 1<<8);
    wmc_set(WMC_RIGHT_INP_PGA_GAIN_CTRL, 1<<8);
    wmc_set(WMC_LEFT_ADC_BOOST_CTRL, (7<<3));
    wmc_set(WMC_RIGHT_ADC_BOOST_CTRL, (7<<3));

    /* Specific to HW clocking */
    wmc_write_masked(WMC_CLOCK_GEN_CTRL, WMC_BCLKDIV_4 | WMC_MS,
                     WMC_BCLKDIV | WMC_MS | WMC_CLKSEL);
    audiohw_set_frequency(HW_FREQ_DEFAULT);

    /* ADC silenced */
    wmc_write_masked(WMC_LEFT_ADC_DIGITAL_VOL, 0x00, WMC_DVOL);
    wmc_write_masked(WMC_RIGHT_ADC_DIGITAL_VOL, 0x00, WMC_DVOL);

    audiohw_enable_headphone_jack(true);
}

void audiohw_set_headphone_vol(int vol_l, int vol_r)
{
    int prev_l = wmc_vol.vol_l;
    int prev_r = wmc_vol.vol_r;
    int dac_l, dac_r;

    wmc_vol.vol_l = vol_l;
    wmc_vol.vol_r = vol_r;

    /* When analogue volume falls below -57dB (0x00) start attenuating the
     * DAC volume */
    if (vol_l >= HW_VOL_DIG_THRESHOLD)
    {
        if (vol_l > HW_VOL_MAX)
            vol_l = HW_VOL_MAX;

        dac_l = HW_VOL_DIG_MAX;
        vol_l -= HW_VOL_DIG_THRESHOLD;
    }
    else
    {
        if (vol_l < HW_VOL_MIN)
            vol_l = HW_VOL_MIN;

        dac_l = 2*vol_l + HW_VOL_DIG_MIN;
        vol_l = HW_VOL_ANA_MIN;
    }

    if (vol_r >= HW_VOL_DIG_THRESHOLD)
    {
        if (vol_r > HW_VOL_MAX)
            vol_r = HW_VOL_MAX;

        dac_r = HW_VOL_DIG_MAX;
        vol_r -= HW_VOL_DIG_THRESHOLD;
    }
    else
    {
        if (vol_r < HW_VOL_MIN)
            vol_r = HW_VOL_MIN;

        dac_r = 2*vol_r + HW_VOL_DIG_MIN;
        vol_r = HW_VOL_ANA_MIN;
    }

    /* Have to write both channels always to have the latching work */
    wmc_write_masked(WMC_LEFT_DAC_DIGITAL_VOL, dac_l, WMC_DVOL);
    wmc_write_masked(WMC_LOUT1_HP_VOLUME_CTRL, vol_l, WMC_AVOL);
    wmc_write_masked(WMC_RIGHT_DAC_DIGITAL_VOL, dac_r, WMC_DVOL);
    wmc_write_masked(WMC_ROUT1_HP_VOLUME_CTRL, vol_r, WMC_AVOL);

    if (wmc_vol.vol_l > HW_VOL_MUTE)
    {
        /* Not muted and going up from mute level? */
        if (prev_l <= HW_VOL_MUTE && !wmc_vol.ahw_mute)
            wmc_clear(WMC_LOUT1_HP_VOLUME_CTRL, WMC_MUTE);
    }
    else
    {
        /* Going to mute level? */
        if (prev_l > HW_VOL_MUTE)
            wmc_set(WMC_LOUT1_HP_VOLUME_CTRL, WMC_MUTE);
    }

    if (wmc_vol.vol_r > HW_VOL_MUTE)
    {
        /* Not muted and going up from mute level? */
        if (prev_r <= HW_VOL_MIN && !wmc_vol.ahw_mute)
            wmc_clear(WMC_ROUT1_HP_VOLUME_CTRL, WMC_MUTE);
    }
    else
    {
        /* Going to mute level? */
        if (prev_r > HW_VOL_MUTE)
            wmc_set(WMC_ROUT1_HP_VOLUME_CTRL, WMC_MUTE);
    }
}

void audiohw_close(void)
{
    /* 1. Mute all analogue outputs */
    audiohw_mute(true);
    audiohw_enable_headphone_jack(false);

    /* 2. Disable power management register 1. R1 = 00 */
    wmc_write(WMC_POWER_MANAGEMENT1, 0x000);

    /* 3. Disable power management register 2. R2 = 00 */
    wmc_write(WMC_POWER_MANAGEMENT2, 0x000);

    /* 4. Disable power management register 3. R3 = 00 */
    wmc_write(WMC_POWER_MANAGEMENT3, 0x000);

    /* 5. Remove external power supplies. */
}

void audiohw_mute(bool mute)
{
    wmc_vol.ahw_mute = mute;

    /* No DAC mute here, please - take care of each enabled output. */
    if (mute)
    {
        wmc_set(WMC_LOUT1_HP_VOLUME_CTRL, WMC_MUTE);
        wmc_set(WMC_ROUT1_HP_VOLUME_CTRL, WMC_MUTE);
    }
    else
    {
        /* Unmute outputs not at mute level */
        if (wmc_vol.vol_l > HW_VOL_MUTE)
            wmc_clear(WMC_LOUT1_HP_VOLUME_CTRL, WMC_MUTE);

        if (wmc_vol.vol_r > HW_VOL_MUTE)
            wmc_clear(WMC_ROUT1_HP_VOLUME_CTRL, WMC_MUTE);
    }
}

void audiohw_set_frequency(int sampling_control)
{
    /* For 16.9344MHz MCLK */
    static const struct
    {
        uint32_t plln  : 8;
        uint32_t pllk1 : 6;
        uint32_t pllk2 : 9;
        uint32_t pllk3 : 9;
        unsigned char mclkdiv;
        unsigned char filter;
    } sctrl_table[HW_NUM_FREQ] =
    {
        [HW_FREQ_8] = /* PLL = 65.536MHz */
        {
            .plln    = WMC_PLLNw(7) | WMC_PLL_PRESCALE,
            .pllk1   = WMC_PLLK_23_18w(12414886ul >> 18),
            .pllk2   = WMC_PLLK_17_9w(12414886ul >> 9),
            .pllk3   = WMC_PLLK_8_0w(12414886ul >> 0),
            .mclkdiv = WMC_MCLKDIV_8,   /*  2.0480 MHz */
            .filter  = WMC_SR_8KHZ,
        },
        [HW_FREQ_11] = /* PLL = off */
        {
            .mclkdiv = WMC_MCLKDIV_6,   /*  2.8224 MHz */
            .filter  = WMC_SR_12KHZ,
        },
        [HW_FREQ_12] = /* PLL = 73.728 MHz */
        {
            .plln    = WMC_PLLNw(8) | WMC_PLL_PRESCALE,
            .pllk1   = WMC_PLLK_23_18w(11869595ul >> 18),
            .pllk2   = WMC_PLLK_17_9w(11869595ul >> 9),
            .pllk3   = WMC_PLLK_8_0w(11869595ul >> 0),
            .mclkdiv = WMC_MCLKDIV_6,   /*  3.0720 MHz */
            .filter  = WMC_SR_12KHZ,
        },
        [HW_FREQ_16] = /* PLL = 65.536MHz */
        {
            .plln    = WMC_PLLNw(7) | WMC_PLL_PRESCALE,
            .pllk1   = WMC_PLLK_23_18w(12414886ul >> 18),
            .pllk2   = WMC_PLLK_17_9w(12414886ul >> 9),
            .pllk3   = WMC_PLLK_8_0w(12414886ul >> 0),
            .mclkdiv = WMC_MCLKDIV_4,   /*  4.0960 MHz */
            .filter  = WMC_SR_16KHZ,
        },
        [HW_FREQ_22] = /* PLL = off */
        {
            .mclkdiv = WMC_MCLKDIV_3,   /*  5.6448 MHz */
            .filter  = WMC_SR_24KHZ,
        },
        [HW_FREQ_24] = /* PLL = 73.728 MHz */
        {
            .plln    = WMC_PLLNw(8) | WMC_PLL_PRESCALE,
            .pllk1   = WMC_PLLK_23_18w(11869595ul >> 18),
            .pllk2   = WMC_PLLK_17_9w(11869595ul >> 9),
            .pllk3   = WMC_PLLK_8_0w(11869595ul >> 0),
            .mclkdiv = WMC_MCLKDIV_3,   /*  6.1440 MHz */
            .filter  = WMC_SR_24KHZ,
        },
        [HW_FREQ_32] = /* PLL = 65.536MHz */
        {
            .plln    = WMC_PLLNw(7) | WMC_PLL_PRESCALE,
            .pllk1   = WMC_PLLK_23_18w(12414886ul >> 18),
            .pllk2   = WMC_PLLK_17_9w(12414886ul >> 9),
            .pllk3   = WMC_PLLK_8_0w(12414886ul >> 0),
            .mclkdiv = WMC_MCLKDIV_2,   /*  8.1920 MHz */
            .filter  = WMC_SR_32KHZ,
        },
        [HW_FREQ_44] = /* PLL = off */
        {
            .mclkdiv = WMC_MCLKDIV_1_5, /* 11.2896 MHz */
            .filter  = WMC_SR_48KHZ,
        },
        [HW_FREQ_48] = /* PLL = 73.728 MHz */
        {
            .plln    = WMC_PLLNw(8) | WMC_PLL_PRESCALE,
            .pllk1   = WMC_PLLK_23_18w(11869595ul >> 18),
            .pllk2   = WMC_PLLK_17_9w(11869595ul >> 9),
            .pllk3   = WMC_PLLK_8_0w(11869595ul >> 0),
            .mclkdiv = WMC_MCLKDIV_1_5, /* 12.2880 MHz */
            .filter  = WMC_SR_48KHZ,
        },
    };

    unsigned int plln;
    unsigned int mclkdiv;

    if ((unsigned)sampling_control >= ARRAYLEN(sctrl_table))
        sampling_control = HW_FREQ_DEFAULT;


    /* Setup filters. */
    wmc_write(WMC_ADDITIONAL_CTRL,
              sctrl_table[sampling_control].filter);

    plln = sctrl_table[sampling_control].plln;
    mclkdiv = sctrl_table[sampling_control].mclkdiv;

    if (plln != 0)
    {
        /* Using PLL to generate SYSCLK */

        /* Program PLL. */
        wmc_write(WMC_PLL_N, plln);
        wmc_write(WMC_PLL_K1, sctrl_table[sampling_control].pllk1);
        wmc_write(WMC_PLL_K2, sctrl_table[sampling_control].pllk2);
        wmc_write(WMC_PLL_K3, sctrl_table[sampling_control].pllk3);

        /* Turn on PLL. */
        wmc_set(WMC_POWER_MANAGEMENT1, WMC_PLLEN);

        /* Switch to PLL and set divider. */
        wmc_write_masked(WMC_CLOCK_GEN_CTRL, mclkdiv | WMC_CLKSEL,
                         WMC_MCLKDIV | WMC_CLKSEL);
    }
    else
    {
        /* Switch away from PLL and set MCLKDIV. */
        wmc_write_masked(WMC_CLOCK_GEN_CTRL, mclkdiv,
                         WMC_MCLKDIV | WMC_CLKSEL);

        /* Turn off PLL. */
        wmc_clear(WMC_POWER_MANAGEMENT1, WMC_PLLEN);
    }
}

#ifdef HAVE_RECORDING
/* TODO */
void audiohw_set_recvol(int left, int right, int type)
{
    (void)left; (void)right; (void)type;
}
#endif
