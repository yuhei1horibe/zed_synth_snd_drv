// SPDX-License-Identifier: GPL-2.0
/*
 * Zedboard ASoC sound card support
 *
 * @author Yuhei Horibe
 * Original code: xlnx_pl_snd_card.c
 * Reference: zed_adau1761.c
 *
 * This sound card driver is specific to Zedboard
 * Both I2S transmitter and I2S receiver device tree nodes
 * have to have phandle to ADAU1761 ("audio-codec" field)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/mutex.h>
#include <linux/types.h>
#include <sound/soc.h>
#include <sound/asequencer.h>
#include <sound/seq_midi_emul.h>

#define I2S_CLOCK_RATIO 1024
#define ZED_MAX_PL_SND_DEV 5
#define ZED_PL_SYNTH_NUM_UNITS 32
#define ZED_PL_SYNTH_MIDI_CH 16

// Linked list is for dynamic unit allocation
struct note_alloc_tracker {
    int8_t note;
    int8_t vel;
    int8_t unit_no;
    struct list_head list;
};

struct zed_pl_card_data {
    // Sound card data
	uint32_t             mclk_val;
	uint32_t             mclk_ratio;
	int                  zed_pl_snd_dev_id;
	struct clk          *mclk;
    struct snd_soc_card *card;
	struct device       *dev;

    // MIDI related data
    //struct snd_card *midi_card; (card->snd_card)
	struct snd_seq_device *seq_dev;
	struct snd_midi_channel_set *chset;
	struct mutex access_mutex;
    int seq_client;
    int busy;
    struct note_alloc_tracker alloc_pool;

    // UIO data
    void __iomem*    addr_base;
    unsigned long    size;
	struct uio_info* info;
};

// Sequencer
int zed_pl_synth_use(void *private_data, struct snd_seq_port_subscribe *info);
int zed_pl_synth_unuse(void *private_data, struct snd_seq_port_subscribe *info);
void zed_pl_synth_free_port(void *private_data);
int zed_pl_synth_event_input(struct snd_seq_event *ev, int direct, void *private_data, int atomic, int hop);

// Midi emulator
void zed_pl_synth_note_on(void *p, int note, int vel, struct snd_midi_channel *chan);
void zed_pl_synth_note_off(void *p, int note, int vel, struct snd_midi_channel *chan);
void zed_pl_synth_key_press(void *p, int note, int vel, struct snd_midi_channel *chan);
void zed_pl_synth_terminate_note(void *p, int note, struct snd_midi_channel *chan);
void zed_pl_synth_control(void *p, int type, struct snd_midi_channel *chan);
void zed_pl_synth_nrpn(void *p, struct snd_midi_channel *chan, struct snd_midi_channel_set *chset);
void zed_pl_synth_sysex(void *p, unsigned char *buf, int len, int parsed, struct snd_midi_channel_set *chset);

// Initialization and release
int zed_pl_synth_init_alloc_pool(struct zed_pl_card_data *prv);
void zed_pl_synth_release_alloc_pool(struct zed_pl_card_data *prv);
void zed_pl_synth_midi_init(void);
void zed_pl_synth_release(struct zed_pl_card_data *prv);
