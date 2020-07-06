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

#include <linux/clk.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/uio_driver.h>

#include "../codecs/adau17x1.h"
#include "../xilinx/xlnx_snd_common.h"

#include "zed_pl_synth.h"

static DEFINE_IDA(zed_snd_card_dev);

static const struct snd_soc_dapm_widget zed_snd_widgets[] = {
    SND_SOC_DAPM_SPK("Line Out", NULL),
    SND_SOC_DAPM_HP("Headphone Out", NULL),
    SND_SOC_DAPM_MIC("Mic In", NULL),
    SND_SOC_DAPM_MIC("Line In", NULL),
};

static const struct snd_soc_dapm_route zed_snd_routes[] = {
    { "Line Out", NULL, "LOUT" },
    { "Line Out", NULL, "ROUT" },
    { "Headphone Out", NULL, "LHP" },
    { "Headphone Out", NULL, "RHP" },
    { "Mic In", NULL, "MICBIAS" },
    { "LINN", NULL, "Mic In" },
    { "RINN", NULL, "Mic In" },
    { "LAUX", NULL, "Line In" },
    { "RAUX", NULL, "Line In" },
};

static const char *zed_snd_card_name = "zed-pl-snd-card";

// Audio CODEC hardware parameter
static int zed_snd_card_hw_params(struct snd_pcm_substream *substream,
                   struct snd_pcm_hw_params *params)
{
    //int ret, clk_div;
    int ret;
    u32 ch, data_width, sample_rate;
    unsigned int pll_rate;
    struct zed_pl_card_data *prv;

    unsigned int fmt;

    struct snd_soc_pcm_runtime *rtd = substream->private_data;
    //struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
    struct snd_soc_dai *codec_dai = rtd->codec_dai;

    dev_info(rtd->dev, "hw_params\n");

    ch = params_channels(params);
    //data_width = params_width(params);
    data_width = 24;
    sample_rate = params_rate(params);

	/* only 2 channels supported */
	if (ch != 2)
		return -EINVAL;

    prv = snd_soc_card_get_drvdata(rtd->card);

    // This is for CODEC DAI
    // Set DAI format
    fmt = SND_SOC_DAIFMT_CBS_CFS | SND_SOC_DAIFMT_I2S;
    ret = snd_soc_dai_set_fmt(codec_dai, fmt);
    if (ret) {
        dev_err(rtd->dev, "Failed to set CODEC DAI format.");
        return ret;
    }

    // TDM settings
    ret = snd_soc_dai_set_tdm_slot(codec_dai, 3, 3, 0, 32);
    if (ret) {
        dev_err(rtd->dev, "Failed to set CODEC TDM slots.");
        return ret;
    }

    // PLL clock/sys clock
    switch (sample_rate) {
    case 48000:
    case 8000:
    case 12000:
    case 16000:
    case 24000:
    case 32000:
    case 96000:
        pll_rate = 48000 * I2S_CLOCK_RATIO;
        break;
    case 44100:
    case 7350:
    case 11025:
    case 14700:
    case 22050:
    case 29400:
    case 88200:
        // Not supported
        pll_rate = 48000 * I2S_CLOCK_RATIO;
        break;
    default:
        return -EINVAL;
    }

    ret = snd_soc_dai_set_pll(codec_dai, ADAU17X1_PLL,
            ADAU17X1_PLL_SRC_MCLK, clk_get_rate(prv->mclk), pll_rate);
    if (ret) {
        dev_err(rtd->dev, "Failed to set CODEC PLL. mclk: %lu, pll_rate: %u", clk_get_rate(prv->mclk), pll_rate);
        return ret;
    }

    ret = snd_soc_dai_set_sysclk(codec_dai, ADAU17X1_CLK_SRC_MCLK, pll_rate,
            SND_SOC_CLOCK_IN);
    if (ret) {
        dev_err(rtd->dev, "Failed to set CODEC sysclk.");
        return ret;
    }

    //ret = snd_soc_dai_set_bclk_ratio(codec_dai, 16);
    //if (ret) {
    //    return ret;
    //}

    // TODO: When sample rate is changed (48k <-> 96k),
    // call synthesizer's driver and change ratio
    //prv->mclk_val = clk_get_rate(prv->mclk);

    return ret;
}

static const struct snd_soc_ops zed_snd_card_ops = {
    .hw_params = zed_snd_card_hw_params,
};

// CPU DAI is dummy because I2S signals are coming from PL
SND_SOC_DAILINK_DEFS(zed_synth_out,
             DAILINK_COMP_ARRAY(COMP_DUMMY()),
             DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "adau-hifi")),
             DAILINK_COMP_ARRAY(COMP_DUMMY()));

static struct snd_soc_dai_link zed_snd_dai = {
        .name        = "zed-synth",
        .stream_name = "zed-synth_out",
        SND_SOC_DAILINK_REG(zed_synth_out),
        .ops = &zed_snd_card_ops,
};

// MIDI initialization

// Register this device as both sound card, and UIO
static int zed_snd_probe(struct platform_device *pdev)
{
    size_t sz;
    char *buf;
    int ret;
    struct snd_soc_dai_link *dai;
    struct zed_pl_card_data *prv;
    struct resource*         res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    struct snd_soc_card *card;

    // ADAU1761 Audio CODEC device node
    struct device_node *pcodec;

    // For MIDI
	struct snd_seq_port_callback callbacks;

    card = devm_kzalloc(&pdev->dev, sizeof(struct snd_soc_card),
                GFP_KERNEL);
    if (!card)
        return -ENOMEM;

    card->dev = &pdev->dev;
    card->dai_link = devm_kzalloc(card->dev,
                      sizeof(*dai),
                      GFP_KERNEL);
    if (!card->dai_link) {
        ret = -ENOMEM;
        goto unreg_class;
    }

    prv = devm_kzalloc(card->dev,
               sizeof(struct zed_pl_card_data),
               GFP_KERNEL);
    if (!prv) {
        ret = -ENOMEM;
        goto unreg_class;
    }
    prv->dev = &pdev->dev;
    prv->card = card;

    // MUTEX
    mutex_init(&prv->access_mutex);

    card->num_links = 0;

    // Audio CODEC device node
    pcodec = of_parse_phandle(pdev->dev.of_node, "audio-codec", 0);

    if (!pcodec) {
        dev_err(card->dev, "Audio CODEC node not found in device tree.\n");
        of_node_put(pcodec);
        return -ENODEV;
    }
    dev_info(card->dev, "ADAU1761 CODEC node found.\n");

    // Get audio master clock node
    prv->mclk = devm_clk_get(&pdev->dev, "aud_mclk");
    if (IS_ERR(prv->mclk)) {
        dev_err(card->dev, "aud_mclk not found in device tree.\n");
        return PTR_ERR(prv->mclk);
    }

    of_node_put(pcodec);

    dai = &card->dai_link[0];
    *dai = zed_snd_dai;
    dai->codecs->of_node = pcodec;
    //dai->cpus->of_node = node;
    card->num_links++;
    snd_soc_card_set_drvdata(card, prv);
    dev_dbg(card->dev, "%s registered\n", card->dai_link[0].name);

    /*
     *  Example : zed-pl-snd-card-0
     *  length = number of chars in "zed-pl-snd-card"
     *      + 1 ('-'), + 1 (card instance num)
     *      + 1 ('\0')
     */
    sz = strlen(zed_snd_card_name) + 3;
    buf = devm_kzalloc(card->dev, sz, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    prv->zed_pl_snd_dev_id = ida_simple_get(&zed_snd_card_dev, 0,
                          ZED_MAX_PL_SND_DEV,
                          GFP_KERNEL);
    if (prv->zed_pl_snd_dev_id < 0)
        return prv->zed_pl_snd_dev_id;

    snprintf(buf, sz, "%s-%d", zed_snd_card_name,
         prv->zed_pl_snd_dev_id);
    card->name = buf;

    // Widgets and routes
    card->dapm_widgets     = zed_snd_widgets;
    card->num_dapm_widgets = ARRAY_SIZE(zed_snd_widgets);
    card->dapm_routes      = zed_snd_routes;
    card->num_dapm_routes  = ARRAY_SIZE(zed_snd_routes);
    card->fully_routed     = true;

    ret = devm_snd_soc_register_card(card->dev, card);
    if (ret) {
        dev_err(card->dev, "%s registration failed\n",
            card->name);
        ida_simple_remove(&zed_snd_card_dev,
                  prv->zed_pl_snd_dev_id);
        return ret;
    }
    dev_info(card->dev, "%s registered\n", card->name);

    if (!res) {
        dev_err(&pdev->dev, "Failed to get platform resource info from device tree.\n");
        ret = -EINVAL;
    }

    // UIO registration
    if(res->start <= 0){
        dev_err(&pdev->dev, "Failed to get device address from device tree.\n");
        ret = -EINVAL;
        goto unreg_class;
    }
    else{
        //dev_info(&pdev->dev, "UIO register base address: %lx\n", (unsigned long)res->start);
        prv->size      = (unsigned long)(resource_size(res));
        prv->addr_base = (void __iomem*)ioremap(res->start, prv->size);
    }

    // UIO info
	prv->info = kzalloc(sizeof(struct uio_info), GFP_KERNEL);
	if (!prv->info) {
        ret = -ENOMEM;
        goto unreg_class;
    }

    prv->info->name                 = zed_snd_card_name;
	prv->info->mem[0].memtype       = UIO_MEM_PHYS;
	prv->info->mem[0].internal_addr = prv->addr_base;
	prv->info->mem[0].addr    = res->start;
	prv->info->mem[0].size    = prv->size;

	prv->info->version   = "0.0.1";
	prv->info->irq       = UIO_IRQ_NONE;
	prv->info->irq_flags = 0;
	prv->info->handler   = NULL;

    // Register device as UIO device
	if (uio_register_device(&pdev->dev, prv->info)) {
        dev_err(&pdev->dev, "Failed to register device as UIO device.\n");
        ret = -EINVAL;
        goto unreg_class;
    }

    // MIDI setup
    // Channel allocation
	prv->chset = snd_midi_channel_alloc_set(ZED_PL_SYNTH_MIDI_CH);
    if (!prv->chset) {
        dev_err(&pdev->dev, "Failed to allocate midi channel.\n");
        ret = -EINVAL;
        goto unreg_class;
    }
    prv->chset->private_data = prv;

    // Kernel sequencer client
    prv->seq_client = snd_seq_create_kernel_client(prv->card->snd_card, prv->zed_pl_snd_dev_id, "Zedbaord PL synth");
    if (prv->seq_client < 0) {
        dev_err(&pdev->dev, "Failed to create sequencer client.\n");
        goto unreg_class;
    }

    // Registration of sequencer callback operations
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.owner        = THIS_MODULE;
	callbacks.use          = zed_pl_synth_use;
	callbacks.unuse        = zed_pl_synth_unuse;
	callbacks.event_input  = zed_pl_synth_event_input;
	callbacks.private_free = zed_pl_synth_free_port;
	callbacks.private_data = prv;

    // Create port
    prv->chset->client = prv->seq_client;
	prv->chset->port   = snd_seq_event_port_attach(prv->seq_client, &callbacks,
						      SNDRV_SEQ_PORT_CAP_WRITE |
						      SNDRV_SEQ_PORT_CAP_SUBS_WRITE,
						      SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC |
						      SNDRV_SEQ_PORT_TYPE_MIDI_GM |
						      SNDRV_SEQ_PORT_TYPE_DIRECT_SAMPLE |
						      SNDRV_SEQ_PORT_TYPE_HARDWARE |
						      SNDRV_SEQ_PORT_TYPE_SYNTHESIZER,
						      ZED_PL_SYNTH_MIDI_CH,
                              ZED_PL_SYNTH_NUM_UNITS,
						      "Zedboard PL synth port");

    if (prv->chset->port < 0) {
        dev_err(&pdev->dev, "Failed to attach sequencer port.");
		snd_midi_channel_free_set(prv->chset);
        return prv->chset->port;
    }

    dev_info(&pdev->dev, "Zedboard PL synthesizer midi module registered");

    dev_set_drvdata(card->dev, prv);

    return 0;

unreg_class:
    if (card) {
        kfree(card->dai_link);
        kfree(card->name);
        kfree(card);
    }
    if (prv) {
        kfree(prv->info);
        kfree(prv);
        if (prv->chset) {
            snd_midi_channel_free_set(prv->chset);
        }
    }
    if (prv->seq_client) {
        snd_seq_delete_kernel_client(prv->seq_client);
    }
    return ret;
}

static int zed_snd_remove(struct platform_device *pdev)
{
    struct zed_pl_card_data *prv = dev_get_drvdata(&pdev->dev);

    ida_simple_remove(&zed_snd_card_dev, prv->zed_pl_snd_dev_id);

    // Unregister UIO device
	uio_unregister_device(prv->info);
	iounmap(prv->addr_base);

    // Free up MIDI resources
    kfree(prv->info);
    kfree(prv);
    if (prv->chset) {
        snd_midi_channel_free_set(prv->chset);
    }
    if (prv->seq_client) {
        snd_seq_delete_kernel_client(prv->seq_client);
    }

    // Release sound card device data
    kfree(prv->card->dai_link);
    kfree(prv->card->name);
    kfree(prv->card);
    return 0;
}

// Device match table
static const struct of_device_id zed_synth_of_ids[] = 
{
    { .compatible = "xlnx,my-synth-1.0" },
    { }
};

static struct platform_driver zed_snd_driver = {
    .driver = {
        .name           = "zed_synth",
        .of_match_table = zed_synth_of_ids,
        .owner          = THIS_MODULE,
    },
    .probe          = zed_snd_probe,
    .remove         = zed_snd_remove,
};

module_platform_driver(zed_snd_driver);

MODULE_DESCRIPTION("Zedboard sound card driver for synthesizer module");
MODULE_AUTHOR("Yuhei Horibe");
MODULE_LICENSE("GPL v2");
