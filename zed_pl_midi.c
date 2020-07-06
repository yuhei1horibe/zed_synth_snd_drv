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
            uint16_t freq;
            uint16_t rsvd;
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
            uint8_t vca_attack;
            uint8_t vca_decay;
            uint8_t vca_sustain;
            uint8_t vca_release;
        } bit;
    } vca_eg_reg;
    union {
        uint32_t amp_reg_all;
        struct {
            uint16_t amp_l;
            uint16_t amp_r;
        } bit;
    } amp_reg;
};

// Linked list is for dynamic unit allocation
struct note_alloc_tracker {
    int8_t note;
    int8_t unit_no;
    struct list_head list;
};

// Per channel data
struct zed_pl_channel_data {
    struct zed_pl_unit_reg    unit_reg;
    int32_t                   vol;
    struct note_alloc_tracker note_alloc;
};

// Per MIDI channel data
static struct zed_pl_channel_data zed_ch_data[ZED_PL_SYNTH_MIDI_CH];

void zed_pl_synth_midi_init()
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

void zed_pl_synth_release(void)
{
    struct note_alloc_tracker *wp1, *wp2;
    int i;

    // Free up list nodes
    for (i = 0; i < ZED_PL_SYNTH_MIDI_CH; i++) {
        list_for_each_entry_safe(wp1, wp2, &(zed_ch_data[i].note_alloc.list), list) {
            list_del(&(wp1->list));
            kfree(wp1);
        }
    }
}

// Allocate free synthesizer unit, and add to note tracker
static int alloc_free_unit(void* reg_base, int ch, int note)
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
    unit_no = alloc_free_unit(prv->addr_base, ch, note);
    if (unit_no >= -1) {
        struct zed_pl_unit_reg *regs = prv->addr_base;

        // Set data
        zed_ch_data[ch].unit_reg.freq_reg.bit.freq   = note_freq[note];
        zed_ch_data[ch].unit_reg.ctl_reg.bit.trigger = true;
        // TODO: Pan
        zed_ch_data[ch].unit_reg.amp_reg.bit.amp_l   = (zed_ch_data[ch].vol * vel) / 127;
        zed_ch_data[ch].unit_reg.amp_reg.bit.amp_r   = (zed_ch_data[ch].vol * vel) / 127;

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
    if (unit_no >= -1) {
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
}

void zed_pl_synth_terminate_note(void *p, int note, struct snd_midi_channel *chan)
{
}

void zed_pl_synth_control(void *p, int type, struct snd_midi_channel *chan)
{
}

void zed_pl_synth_nrpn(void *p, struct snd_midi_channel *chan, struct snd_midi_channel_set *chset)
{
}

void zed_pl_synth_sysex(void *p, unsigned char *buf, int len, int parsed, struct snd_midi_channel_set *chset)
{
}
