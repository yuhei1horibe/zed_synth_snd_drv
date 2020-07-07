// SPDX-License-Identifier: GPL-2.0
/*
 * Zedboard PL synthesizer MIDI driver
 *
 * @author Yuhei Horibe
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#include "zed_pl_synth.h"
#include <linux/types.h>
#include <linux/module.h>
#include <sound/asoundef.h>

#define ZED_PL_NOTE_MAX 127

enum zed_pl_wave_type {
    ZED_PL_WAVE_SQUARE = 0,
    ZED_PL_WAVE_SAW    = 1,
    ZED_PL_WAVE_TRI    = 2,
    ZED_PL_WAVE_RSVD   = 3, // Not defined yet
};

// Frequency table
static const int note_freq[] = {
    8, 9, 9, 10, 10, 11, 12, 12, 13, 14, 15, 
    15, 16, 17, 18, 19, 21, 22, 23, 24, 26, 28, 29, 
    31, 33, 35, 37, 39, 41, 44, 46, 49, 52, 55, 58, 
    62, 65, 69, 73, 78, 82, 87, 92, 98, 104, 110, 117, 
    123, 131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 
    247, 262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 
    494, 523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 
    988, 1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 
    1976, 2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 
    3951, 4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 
    7902, 8372, 8870, 9397, 9956, 10548, 11175, 11840, 12544
};

// Register map
// Register map per synthesizer unit
struct zed_pl_unit_reg {
    union {
        uint32_t freq_reg_all;
        struct {
            uint32_t freq : 16;
            uint32_t rsvd : 16;
        } bit;
    } freq_reg;
    union {
        uint32_t ctl_reg_all;
        struct {
            uint32_t wave_type : 2;
            uint32_t trigger   : 1;
            uint32_t rsvd      : 29;
        } bit;
    } ctl_reg;
    union {
        uint32_t vca_eg_reg_all;
        struct {
            uint32_t vca_attack  : 8;
            uint32_t vca_decay   : 8;
            uint32_t vca_sustain : 8;
            uint32_t vca_release : 8;
        } bit;
    } vca_eg_reg;
    union {
        uint32_t amp_reg_all;
        struct {
            uint32_t amp_l : 16;
            uint32_t amp_r : 16;
        } bit;
    } amp_reg;
};

// Linked list is for dynamic unit allocation
struct note_alloc_tracker {
    int8_t note;
    int8_t vel;
    int8_t unit_no;
    struct list_head list;
};

// Per channel data
struct zed_pl_channel_data {
    struct zed_pl_unit_reg    unit_reg;
    int8_t                    vol;
    int8_t                    exp;
    int8_t                    pan;
    int8_t                    mod;

    // Calculated volume
    int16_t                   vol_l;
    int16_t                   vol_r;
    struct note_alloc_tracker note_alloc;
};

// Per MIDI channel data
static struct zed_pl_channel_data zed_ch_data[ZED_PL_SYNTH_MIDI_CH];

static void zed_pl_synth_calc_vol(int ch, int vel)
{
    int8_t  vol;
    int8_t  exp;
    int8_t  pan;
    int32_t calc;

    if ((ch >=ZED_PL_SYNTH_MIDI_CH) || (ch < 0)) {
        return ;
    }
    vol = zed_ch_data[ch].vol;
    exp = zed_ch_data[ch].exp;
    pan = zed_ch_data[ch].pan;

    calc = ((int32_t)vol * vel * exp) / 16129; // 127 ^ 2
    zed_ch_data[ch].vol_l = (calc * (128 - pan)) / 64;
    zed_ch_data[ch].vol_r = (calc * pan) / 64;

    return ;
}

void zed_pl_synth_midi_init(void)
{
    int i;
    memset(zed_ch_data, 0, sizeof(struct zed_pl_channel_data) * ZED_PL_SYNTH_MIDI_CH);

    // Unit allocation tracker initialization
    for (i = 0; i < ZED_PL_SYNTH_MIDI_CH; i++) {
        INIT_LIST_HEAD(&(zed_ch_data[i].note_alloc.list));
    }

    // Set default values for channel data
    for (i = 0; i < ZED_PL_SYNTH_MIDI_CH; i++) {
        struct zed_pl_unit_reg *reg = &zed_ch_data[i].unit_reg;
        zed_ch_data[i].vol = 100;
        reg->ctl_reg.bit.wave_type      = ZED_PL_WAVE_SAW;
        reg->vca_eg_reg.bit.vca_attack  = 0x40;
        reg->vca_eg_reg.bit.vca_decay   = 0x20;
        reg->vca_eg_reg.bit.vca_sustain = 0x40;
        reg->vca_eg_reg.bit.vca_release = 0x8;
    }
}

void zed_pl_synth_release(struct zed_pl_card_data *prv)
{
    struct note_alloc_tracker *wp1, *wp2;
    int i;
    struct zed_pl_unit_reg *regs = NULL;

    if (!prv) {
        return ;
    }
    regs = prv->addr_base;

    // Free up list nodes
    for (i = 0; i < ZED_PL_SYNTH_MIDI_CH; i++) {
        list_for_each_entry_safe(wp1, wp2, &(zed_ch_data[i].note_alloc.list), list) {
            int unit_no = wp1->unit_no;
            list_del(&(wp1->list));
            kfree(wp1);

            // Release unit
            zed_ch_data[i].unit_reg.freq_reg.bit.freq = 0;
            zed_ch_data[i].unit_reg.ctl_reg.bit.trigger = false;
            // NOTE: For release, don't touch amplitude
            //zed_ch_data[ch].unit_reg.amp_reg.bit.amp_l = 0;
            //zed_ch_data[ch].unit_reg.amp_reg.bit.amp_r = 0;

            // Write to register
            regs[unit_no] = zed_ch_data[i].unit_reg;
        }
        INIT_LIST_HEAD(&zed_ch_data[i].note_alloc.list);
    }
}

// MIDI reset event (GM/GS/XG reset)
void zed_pl_synth_midi_reset_event(struct zed_pl_card_data *prv)
{
    zed_pl_synth_release(prv);
    zed_pl_synth_midi_init();
}

// Allocate free synthesizer unit, and add to note tracker
static int alloc_free_unit(void* reg_base, int ch, int note, int vel)
{
    static int cur_pos = 0; // For round robin
    struct zed_pl_unit_reg *regs = reg_base;
    int i;

    if (!reg_base) {
        return -1;
    }

    for (i = 1; i <= ZED_PL_SYNTH_NUM_UNITS; i++) {
        int reg_off = (cur_pos + i) % ZED_PL_SYNTH_NUM_UNITS;
        if (regs[reg_off].ctl_reg.bit.trigger == false) {
            struct note_alloc_tracker *note_track = kzalloc(sizeof(struct note_alloc_tracker), GFP_KERNEL);
            if (!note) {
                return -1;
            }

            cur_pos = reg_off;
            note_track->unit_no = cur_pos;
            note_track->note    = note;
            note_track->vel     = vel;

            // Add entry for note tracker
            list_add_tail(&(note_track->list), &zed_ch_data[ch].note_alloc.list);
            return cur_pos;
        }
    }
    return -1;
}

void zed_pl_synth_note_on(void *p, int note, int vel, struct snd_midi_channel *chan)
{
    struct zed_pl_card_data *prv = p;
    int unit_no = -1;
    int ch = 0;
    
    if (!chan) {
        return ;
    }

    ch = chan->number;
    if ((ch >= ZED_PL_SYNTH_MIDI_CH) || (ch < 0)) {
        return ;
    }

    if ((note >= ZED_PL_NOTE_MAX) || (note < 0)) {
        return ;
    }

    mutex_lock(&prv->access_mutex);

    // Allocate unit and add entry to tracker
    unit_no = alloc_free_unit(prv->addr_base, ch, note, vel);
    if (unit_no >= 0) {
        struct zed_pl_unit_reg *regs = prv->addr_base;

        // Calculate volume
        zed_pl_synth_calc_vol(ch, vel);

        // Set data
        zed_ch_data[ch].unit_reg.freq_reg.bit.freq   = note_freq[note];
        zed_ch_data[ch].unit_reg.ctl_reg.bit.trigger = true;
        zed_ch_data[ch].unit_reg.amp_reg.bit.amp_l   = zed_ch_data[ch].vol_l;
        zed_ch_data[ch].unit_reg.amp_reg.bit.amp_r   = zed_ch_data[ch].vol_r;

        // Write to register
        regs[unit_no] = zed_ch_data[ch].unit_reg;
    }
    mutex_unlock(&prv->access_mutex);
}

static int free_unit(int ch, int note)
{
    struct note_alloc_tracker *wp1, *wp2;
    if (ch >= ZED_PL_SYNTH_MIDI_CH) {
        return -1;
    }

    if (note >= ZED_PL_NOTE_MAX) {
        return -1;
    }
    list_for_each_entry_safe(wp1, wp2, &(zed_ch_data[ch].note_alloc.list), list) {
        if (wp1->note == note) {
            int unit_no = wp1->unit_no;
            list_del(&(wp1->list));
            kfree(wp1);
            return unit_no;
        }
    }
    return -1;
}

void zed_pl_synth_note_off(void *p, int note, int vel, struct snd_midi_channel *chan)
{
    struct zed_pl_card_data *prv = p;
    int unit_no = -1;

    if (chan->number >= ZED_PL_SYNTH_MIDI_CH) {
        return ;
    }

    if (note >= ZED_PL_NOTE_MAX) {
        return ;
    }

    mutex_lock(&prv->access_mutex);

    // Find target unit
    unit_no = free_unit(chan->number, note);
    if (unit_no >= 0) {
        struct zed_pl_unit_reg *regs = prv->addr_base;
        const int ch = chan->number;

        // Set data
        zed_ch_data[ch].unit_reg.freq_reg.bit.freq = 0;
        zed_ch_data[ch].unit_reg.ctl_reg.bit.trigger = false;
        // NOTE: For release, don't touch amplitude
        //zed_ch_data[ch].unit_reg.amp_reg.bit.amp_l = 0;
        //zed_ch_data[ch].unit_reg.amp_reg.bit.amp_r = 0;

        // Write to register
        regs[unit_no] = zed_ch_data[ch].unit_reg;
    }
    mutex_unlock(&prv->access_mutex);
}

void zed_pl_synth_key_press(void *p, int note, int vel, struct snd_midi_channel *chan)
{
    zed_pl_synth_note_on(p, note, vel, chan);
}

void zed_pl_synth_terminate_note(void *p, int note, struct snd_midi_channel *chan)
{
    zed_pl_synth_note_off(p, note, 0, chan);
}

// Handle control change and program change
void zed_pl_synth_control(void *p, int type, struct snd_midi_channel *chan)
{
    struct zed_pl_card_data *prv  = p;
    struct zed_pl_unit_reg  *regs = NULL;
    int ch = chan->number;
    struct note_alloc_tracker *wp;

    if (prv == NULL) {
        return ;
    }
    regs = prv->addr_base;

    if ((ch >= ZED_PL_SYNTH_MIDI_CH) || (ch < 0)) {
        return ;
    }

    // Control change
    mutex_lock(&prv->access_mutex);
    zed_ch_data[ch].vol = chan->gm_volume;
    zed_ch_data[ch].exp = chan->gm_expression;
    zed_ch_data[ch].pan = chan->gm_pan;
    zed_ch_data[ch].mod = chan->gm_modulation_wheel_lsb;

    // Change volume (TODO: Frequency)
    list_for_each_entry (wp, &zed_ch_data[ch].note_alloc.list, list) {
        int unit_no = wp->unit_no;

        zed_pl_synth_calc_vol(ch, wp->vel);
        zed_ch_data[ch].unit_reg.amp_reg.bit.amp_l   = zed_ch_data[ch].vol_l;
        zed_ch_data[ch].unit_reg.amp_reg.bit.amp_r   = zed_ch_data[ch].vol_r;

        // Write volume
        regs[unit_no].amp_reg.amp_reg_all = zed_ch_data[ch].unit_reg.amp_reg.amp_reg_all;
    }
    mutex_unlock(&prv->access_mutex);
}

void zed_pl_synth_nrpn(void *p, struct snd_midi_channel *chan, struct snd_midi_channel_set *chset)
{
}

void zed_pl_synth_sysex(void *p, unsigned char *buf, int len, int parsed, struct snd_midi_channel_set *chset)
{
    struct zed_pl_card_data *prv  = p;

    // Handle GM/GS/XG resets only
    switch (parsed) {
    case SNDRV_MIDI_SYSEX_GM_ON:
    case SNDRV_MIDI_MODE_GS:
    case SNDRV_MIDI_MODE_XG:
        mutex_lock(&prv->access_mutex);
        zed_pl_synth_midi_reset_event(p);
        mutex_unlock(&prv->access_mutex);
        break;
    }
}
