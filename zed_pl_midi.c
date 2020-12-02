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

// Preset tone parameters
struct zed_pl_params {
    uint32_t wave_type;
    union {
        struct {
            uint32_t vca_attack  : 8;
            uint32_t vca_decay   : 8;
            uint32_t vca_sustain : 8;
            uint32_t vca_release : 8;
        } bit;
        uint32_t vca_eg_all;
    } vca_eg;
};

// Preset parameters
static struct zed_pl_params zed_pl_synth_preset_tones[] = {
    { 0, {{ 0x80, 0x02, 0x08, 0x02 }}, }, // 001: Acoustic grand
    { 1, {{ 0x80, 0x02, 0x08, 0x02 }}, }, // 002: Bright acoustic
    { 2, {{ 0x80, 0x02, 0x40, 0x02 }}, }, // 003: Electric grand
    { 1, {{ 0x40, 0x02, 0x40, 0x02 }}, }, // 004: Honky tonk
    { 2, {{ 0x20, 0x02, 0x30, 0x02 }}, }, // 005: Electric Piano 1
    { 2, {{ 0x10, 0x02, 0x30, 0x02 }}, }, // 006: Electric Piano 2
    { 1, {{ 0x80, 0x10, 0x20, 0x02 }}, }, // 007: Harpsichord
    { 1, {{ 0x80, 0x10, 0x20, 0x02 }}, }, // 008: Clavinet

    { 2, {{ 0x80, 0x01, 0x20, 0x01 }}, }, // 009: Celesta
    { 2, {{ 0x80, 0x01, 0x20, 0x01 }}, }, // 010: Glockenspiel
    { 2, {{ 0x80, 0x01, 0x20, 0x01 }}, }, // 011: Music Box
    { 2, {{ 0x80, 0x01, 0x20, 0x01 }}, }, // 012: Vibraphone
    { 2, {{ 0x80, 0x01, 0x20, 0x01 }}, }, // 013: Marimba
    { 2, {{ 0x80, 0x01, 0x20, 0x01 }}, }, // 014: Xylophone
    { 2, {{ 0x80, 0x01, 0x20, 0x01 }}, }, // 015: Tubular Bells
    { 2, {{ 0x80, 0x01, 0x20, 0x01 }}, }, // 016: Dulcimer

    { 1, {{ 0xFF, 0x10, 0x40, 0x01 }}, }, // 017: Drawbar Organ
    { 1, {{ 0xFF, 0x10, 0x40, 0x01 }}, }, // 018: Percussive Organ
    { 1, {{ 0xFF, 0x10, 0x40, 0x01 }}, }, // 019: Rock Organ
    { 1, {{ 0xFF, 0x10, 0x40, 0x01 }}, }, // 020: Church Organ
    { 1, {{ 0xFF, 0x10, 0x40, 0x01 }}, }, // 021: Reed Organ
    { 1, {{ 0xFF, 0x10, 0x40, 0x01 }}, }, // 022: Accoridan
    { 1, {{ 0xFF, 0x10, 0x40, 0x01 }}, }, // 023: Harmonica
    { 1, {{ 0xFF, 0x10, 0x40, 0x01 }}, }, // 024: Tango Accordian

    { 0, {{ 0x80, 0x04, 0x08, 0x02 }}, }, // 025: Nylon String Guitar
    { 0, {{ 0xC0, 0x04, 0x08, 0x02 }}, }, // 026: Steel String Guitar
    { 0, {{ 0x80, 0x04, 0x08, 0x02 }}, }, // 027: Electric Jazz Guitar
    { 0, {{ 0x80, 0x04, 0x08, 0x02 }}, }, // 028: Electric Clean Guitar
    { 0, {{ 0x80, 0x04, 0x04, 0x02 }}, }, // 029: Electric Muted Guitar
    { 0, {{ 0x80, 0x40, 0x40, 0x02 }}, }, // 030: Overdriven Guitar
    { 0, {{ 0xA0, 0x40, 0x40, 0x02 }}, }, // 031: Distortion Guitar
    { 0, {{ 0x80, 0x04, 0x08, 0x02 }}, }, // 032: Guitar Harmonics

    { 2, {{ 0x40, 0x08, 0x08, 0x02 }}, }, // 033: Acoustic Bass
    { 0, {{ 0xC0, 0x08, 0x10, 0x02 }}, }, // 034: Electric Bass(finger)
    { 0, {{ 0x80, 0x08, 0x20, 0x02 }}, }, // 035: Electric Bass(pick)
    { 0, {{ 0x40, 0x08, 0x20, 0x02 }}, }, // 036: Fretless Bass
    { 1, {{ 0x80, 0x08, 0x30, 0x02 }}, }, // 037: Slap Bass 1
    { 1, {{ 0x80, 0x08, 0x30, 0x02 }}, }, // 038: Slap Bass 2
    { 2, {{ 0xFF, 0x08, 0x30, 0x02 }}, }, // 039: Synth Bass 1
    { 2, {{ 0xFF, 0x08, 0x30, 0x02 }}, }, // 040: Synth Bass 2

    { 1, {{ 0x10, 0x02, 0x80, 0x02 }}, }, // 041: Violin
    { 1, {{ 0x10, 0x02, 0x80, 0x02 }}, }, // 042: Viola
    { 1, {{ 0x10, 0x02, 0x80, 0x02 }}, }, // 043: Cello
    { 1, {{ 0x10, 0x02, 0x80, 0x02 }}, }, // 044: Contrabass
    { 1, {{ 0x40, 0x08, 0x04, 0x02 }}, }, // 045: Tremolo Strings
    { 1, {{ 0x40, 0x08, 0x04, 0x02 }}, }, // 046: Pizzicato Strings
    { 1, {{ 0x10, 0x08, 0x80, 0x02 }}, }, // 047: Orchestral Strings
    { 2, {{ 0x40, 0x08, 0x08, 0x02 }}, }, // 048: Timpani

    { 1, {{ 0x08, 0x01, 0x80, 0x01 }}, }, // 049: String Ensemble 1
    { 1, {{ 0x08, 0x01, 0x80, 0x01 }}, }, // 050: String Ensemble 2
    { 1, {{ 0x04, 0x01, 0x80, 0x01 }}, }, // 051: SynthStrings 1
    { 1, {{ 0x08, 0x01, 0x80, 0x02 }}, }, // 052: SynthStrings 2
    { 1, {{ 0x20, 0x01, 0x70, 0x02 }}, }, // 053: Choir Aahs
    { 1, {{ 0x20, 0x01, 0x70, 0x02 }}, }, // 054: Voice Oohs
    { 1, {{ 0x20, 0x01, 0x70, 0x02 }}, }, // 055: Synth Voice
    { 1, {{ 0xA0, 0x10, 0x08, 0x20 }}, }, // 056: Orchestra Hit

    { 1, {{ 0xA0, 0x20, 0x40, 0x10 }}, }, // 057: Trumpet
    { 1, {{ 0xA0, 0x20, 0x40, 0x10 }}, }, // 058: Trombone
    { 1, {{ 0xA0, 0x20, 0x40, 0x10 }}, }, // 059: Tuba
    { 1, {{ 0xA0, 0x20, 0x08, 0x10 }}, }, // 060: Muted Trumpet
    { 1, {{ 0xA0, 0x20, 0x40, 0x10 }}, }, // 061: French Horn
    { 1, {{ 0xA0, 0x20, 0x40, 0x10 }}, }, // 062: Brass Section
    { 1, {{ 0xA0, 0x20, 0x40, 0x10 }}, }, // 063: SynthBrass 1
    { 1, {{ 0xA0, 0x20, 0x40, 0x10 }}, }, // 064: SynthBrass 2

    { 1, {{ 0xA0, 0x40, 0x20, 0x08 }}, }, // 065: Soprano Sax
    { 1, {{ 0xA0, 0x40, 0x20, 0x08 }}, }, // 066: Alto Sax
    { 1, {{ 0xA0, 0x40, 0x20, 0x08 }}, }, // 067: Tenor Sax
    { 1, {{ 0xA0, 0x40, 0x30, 0x08 }}, }, // 068: Baritone Sax
    { 1, {{ 0x40, 0x20, 0x40, 0x08 }}, }, // 069: Oboe
    { 1, {{ 0x10, 0x40, 0x40, 0x08 }}, }, // 070: English Horn
    { 1, {{ 0x10, 0x40, 0x40, 0x08 }}, }, // 071: Bassoon
    { 1, {{ 0xA0, 0x40, 0x40, 0x08 }}, }, // 072: Clarinet

    { 2, {{ 0x70, 0x20, 0x80, 0x08 }}, }, // 073: Piccolo
    { 2, {{ 0x20, 0x10, 0x40, 0x08 }}, }, // 074: Flute
    { 2, {{ 0x70, 0x20, 0x80, 0x08 }}, }, // 075: Recorder
    { 2, {{ 0xC0, 0x20, 0x30, 0x08 }}, }, // 076: Pan Flute
    { 2, {{ 0x30, 0x20, 0x40, 0x08 }}, }, // 077: Blown Bottle
    { 2, {{ 0x40, 0x20, 0x20, 0x08 }}, }, // 078: Shakuhachi
    { 2, {{ 0x70, 0x20, 0x40, 0x08 }}, }, // 079: Whistle
    { 2, {{ 0x40, 0x20, 0x40, 0x08 }}, }, // 080: Ocarina

    { 0, {{ 0x80, 0x20, 0x20, 0x08 }}, }, // 081: Square Wave
    { 1, {{ 0x80, 0x10, 0x40, 0x08 }}, }, // 082: Saw Wave
    { 2, {{ 0x80, 0x04, 0x80, 0x08 }}, }, // 083: Syn. Calliope
    { 0, {{ 0x80, 0x20, 0x40, 0x08 }}, }, // 084: Chiffer Lead
    { 0, {{ 0x80, 0x20, 0x40, 0x08 }}, }, // 085: Charang
    { 2, {{ 0x80, 0x20, 0x40, 0x08 }}, }, // 086: Solo Vox
    { 1, {{ 0x80, 0x20, 0x40, 0x08 }}, }, // 087: 5th Saw Wave
    { 1, {{ 0x80, 0x20, 0x40, 0x08 }}, }, // 088: Bass& Lead

    { 2, {{ 0x02, 0x02, 0x40, 0x02 }}, }, // 089: Fantasia
    { 1, {{ 0x02, 0x02, 0x40, 0x02 }}, }, // 090: Warm Pad
    { 2, {{ 0x02, 0x02, 0x40, 0x02 }}, }, // 091: Polysynth
    { 2, {{ 0x02, 0x02, 0x40, 0x02 }}, }, // 092: Space Voice
    { 2, {{ 0x02, 0x02, 0x40, 0x02 }}, }, // 093: Bowed Glass
    { 0, {{ 0x02, 0x02, 0x20, 0x02 }}, }, // 094: Metal Pad
    { 2, {{ 0x02, 0x02, 0x40, 0x02 }}, }, // 095: Halo Pad
    { 1, {{ 0x02, 0x02, 0x40, 0x02 }}, }, // 096: Sweep Pad

    { 1, {{ 0xFF, 0x20, 0x40, 0x02 }}, }, // 097: Ice Rain
    { 1, {{ 0x02, 0x02, 0x40, 0x02 }}, }, // 098: Soundtrack
    { 2, {{ 0xFF, 0x04, 0x40, 0x04 }}, }, // 099: Crystal
    { 1, {{ 0x02, 0x02, 0x40, 0x02 }}, }, // 100: Atmosphere
    { 1, {{ 0x02, 0x02, 0x40, 0x02 }}, }, // 101: Brightness
    { 2, {{ 0x02, 0x02, 0x40, 0x02 }}, }, // 102: Goblin
    { 1, {{ 0x02, 0x02, 0x40, 0x02 }}, }, // 103: Echo Drops
    { 1, {{ 0x02, 0x02, 0x40, 0x02 }}, }, // 104: Star Theme

    { 1, {{ 0x80, 0x20, 0x20, 0x08 }}, }, // 105: Sitar
    { 0, {{ 0x80, 0x40, 0x40, 0x08 }}, }, // 106: Banjo
    { 0, {{ 0xC0, 0x40, 0x04, 0x08 }}, }, // 107: Shamisen
    { 2, {{ 0xA0, 0x02, 0x10, 0x08 }}, }, // 108: Koto
    { 1, {{ 0x80, 0x02, 0x10, 0x08 }}, }, // 109: Kalimba
    { 1, {{ 0x80, 0x40, 0x40, 0x08 }}, }, // 110: Bagpipe
    { 1, {{ 0x20, 0x10, 0x30, 0x08 }}, }, // 111: Fiddle
    { 1, {{ 0x40, 0x20, 0x40, 0x08 }}, }, // 112: Shanai

    { 1, {{ 0x80, 0x10, 0x40, 0x08 }}, }, // 121: Guitar Fret Noise
    { 1, {{ 0x80, 0x10, 0x40, 0x08 }}, }, // 122: Breath Noise
    { 1, {{ 0x80, 0x10, 0x40, 0x08 }}, }, // 123: Seashore
    { 1, {{ 0x80, 0x10, 0x40, 0x08 }}, }, // 124: Bird Tweet
    { 1, {{ 0x80, 0x10, 0x40, 0x08 }}, }, // 125: Telephone Ring
    { 1, {{ 0x80, 0x10, 0x40, 0x08 }}, }, // 126: Helicopter
    { 1, {{ 0x80, 0x10, 0x40, 0x08 }}, }, // 127: Applause
    { 1, {{ 0x80, 0x10, 0x40, 0x08 }}, }, // 128: Gunshot
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

struct zed_pl_common_reg {
    union {
        uint32_t audio_ctl_all;
        struct {
            uint32_t aud_clk_sel : 1;
            uint32_t rsvd        : 31;
        } bit;
    } audio_ctl_reg;
    uint32_t unit_free_reg;
};

// Per channel data
struct zed_pl_channel_data {
    struct zed_pl_unit_reg    unit_reg;
    int8_t                    vol;
    int8_t                    exp;
    int8_t                    pan;
    int8_t                    mod;

    // Instrument
    int8_t                    midi_program;

    // Calculated volume
    int16_t                   vol_l;
    int16_t                   vol_r;
    struct note_alloc_tracker note_alloc;
};
static const int ZED_PL_COMMON_REG_OFF = ZED_PL_SYNTH_NUM_UNITS * sizeof(struct zed_pl_unit_reg) / sizeof(uint32_t);

// Per MIDI channel data
static struct zed_pl_channel_data zed_ch_data[ZED_PL_SYNTH_MIDI_CH];

void zed_pl_synth_release_alloc_pool(struct zed_pl_card_data *prv)
{
    struct note_alloc_tracker *wp1, *wp2;
    struct zed_pl_unit_reg *regs = NULL;

    if (!prv) {
        return ;
    }
    regs = prv->addr_base;

    // Release all notes
    zed_pl_synth_release(prv);

    // Free up all list nodes
    list_for_each_entry_safe(wp1, wp2, &(prv->alloc_pool.list), list) {
        list_del(&(wp1->list));
        kfree(wp1);
    }
    INIT_LIST_HEAD(&prv->alloc_pool.list);
}

// MIDI pool allocation
int zed_pl_synth_init_alloc_pool(struct zed_pl_card_data *prv)
{
    int i;
    struct note_alloc_tracker *wp = NULL;

    INIT_LIST_HEAD(&prv->alloc_pool.list);

    for (i = 0; i < ZED_PL_SYNTH_NUM_UNITS; i++) {
        wp = kzalloc(sizeof(struct note_alloc_tracker), GFP_KERNEL);
        if (wp == NULL) {
            zed_pl_synth_release_alloc_pool(prv);
            return -1;
        }
        list_add_tail(&wp->list, &prv->alloc_pool.list);
    }
    return 0;
}

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

    calc = ((int32_t)vol * vel * exp) / 32258; // 127 ^ 2
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
            // Return the node to the pool
            list_move_tail(&(wp1->list), &(prv->alloc_pool.list));

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
static int alloc_free_unit(struct zed_pl_card_data *prv, int ch, int note, int vel)
{
    static int cur_pos = 0; // For round robin
    struct zed_pl_common_reg *regs = NULL;
    int i;

    if (!prv || !prv->addr_base) {
        return -1;
    }
    regs = prv->addr_base + ZED_PL_COMMON_REG_OFF;

    for (i = 1; i <= ZED_PL_SYNTH_NUM_UNITS; i++) {
        int reg_off = (cur_pos + i) % ZED_PL_SYNTH_NUM_UNITS;
        if ((regs->unit_free_reg & (1 << reg_off)) == 0) {
            struct note_alloc_tracker *note_track = list_first_entry(&(prv->alloc_pool.list), struct note_alloc_tracker, list);
            if (!note_track) {
                return -1;
            }

            cur_pos = reg_off;
            note_track->unit_no = cur_pos;
            note_track->note    = note;
            note_track->vel     = vel;

            // Add entry for note tracker
            list_move_tail(&note_track->list, &zed_ch_data[ch].note_alloc.list);
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

    if (chan->drum_channel == 0) {
        mutex_lock(&prv->access_mutex);

        // Program change
        if (chan->midi_program != zed_ch_data[ch].midi_program) {
            zed_pl_synth_program_change(prv, ch, chan->midi_program);
        }

        // Allocate unit and add entry to tracker
        unit_no = alloc_free_unit(prv, ch, note, vel);
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
    } //else {
        // TODO
    //}
}

static int free_unit(struct zed_pl_card_data *prv, int ch, int note)
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

            // Return the free node to the pool
            list_move_tail(&(wp1->list), &(prv->alloc_pool.list));
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

    if (chan->drum_channel == 0) {
        mutex_lock(&prv->access_mutex);

        // Find target unit
        unit_no = free_unit(prv, chan->number, note);
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
}

void zed_pl_synth_key_press(void *p, int note, int vel, struct snd_midi_channel *chan)
{
    zed_pl_synth_note_on(p, note, vel, chan);
}

void zed_pl_synth_terminate_note(void *p, int note, struct snd_midi_channel *chan)
{
    zed_pl_synth_note_off(p, note, 0, chan);
}

// Program change
void zed_pl_synth_program_change(struct zed_pl_card_data *prv, int ch, int pgm_num)
{
    if (!prv) {
        return ;
    }

    if ((ch < 0) || (ch >= ZED_PL_SYNTH_MIDI_CH)) {
        return ;
    }

    if ((pgm_num < 0) || (pgm_num > 127)) {
        return ;
    }

    zed_ch_data[ch].unit_reg.ctl_reg.ctl_reg_all       = zed_pl_synth_preset_tones[pgm_num].wave_type;
    zed_ch_data[ch].unit_reg.vca_eg_reg.vca_eg_reg_all = zed_pl_synth_preset_tones[pgm_num].vca_eg.vca_eg_all;
    zed_ch_data[ch].midi_program                       = pgm_num;
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
