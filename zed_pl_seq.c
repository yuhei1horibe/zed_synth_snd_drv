// SPDX-License-Identifier: GPL-2.0
/*
 * Zedboard PL Synthesizer module driver (sequencer)
 *
 * @author Yuhei Horibe
 * This is midi sequencer initialization part of
 * Zedboard PL synthesizer device driver
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/moduleparam.h>
#include <linux/module.h>
#include <sound/initval.h>
#include <sound/asoundef.h>
#include "zed_pl_synth.h"

// MIDI event handlers
static struct snd_midi_op zed_pl_synth_ops = {
    .note_on        = zed_pl_synth_note_on,
    .note_off       = zed_pl_synth_note_off,
    .note_terminate = zed_pl_synth_terminate_note,
    .control        = zed_pl_synth_control,
    .nrpn           = zed_pl_synth_nrpn,
    .sysex          = zed_pl_synth_sysex,
};

// Sequencer callbacks
int zed_pl_synth_use(void *private_data, struct snd_seq_port_subscribe *info)
{
    struct zed_pl_card_data *prv = (struct zed_pl_card_data*)private_data;
    int ret = 0;

    mutex_lock(&prv->access_mutex);

    if (prv->busy) {
        mutex_unlock(&prv->access_mutex);
        dev_err(prv->dev, "Device is busy.\n");
        return -EBUSY;
    }
    prv->busy = 1;

    if (!try_module_get(prv->card->snd_card->module)) {
        dev_err(prv->dev, "Failed to get module.\n");
        ret = -EFAULT;
    }
    zed_pl_synth_midi_init();

    mutex_unlock(&prv->access_mutex);
    return ret;
}

int zed_pl_synth_unuse(void *private_data, struct snd_seq_port_subscribe *info)
{
    struct zed_pl_card_data *prv = (struct zed_pl_card_data*)private_data;
    int ret = 0;

    mutex_lock(&prv->access_mutex);
    zed_pl_synth_release(prv);
    prv->busy = 0;
    if (info->sender.client != SNDRV_SEQ_CLIENT_SYSTEM) {
        module_put(prv->card->snd_card->module);
    }
    mutex_unlock(&prv->access_mutex);
    return ret;
}

void zed_pl_synth_free_port(void *private_data)
{
    struct zed_pl_card_data *prv = (struct zed_pl_card_data*)private_data;

    mutex_lock(&prv->access_mutex);
    zed_pl_synth_release(prv);
    mutex_unlock(&prv->access_mutex);
    snd_midi_channel_free_set(prv->chset);
}

// MIDI event handler
int zed_pl_synth_event_input(struct snd_seq_event *ev, int direct, void *private_data, int atomic, int hop)
{
    struct zed_pl_card_data *prv = (struct zed_pl_card_data*)private_data;

    snd_midi_process_event(&zed_pl_synth_ops, ev, prv->chset);
    return 0;
}

