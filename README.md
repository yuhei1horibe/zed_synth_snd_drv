# zed_synth_snd_drv
ALSA machine driver for synthesizer module on Zedboard.

This machine driver will use ADAU1761 as CODEC DAI, and "snd-soc-dummy" as CPU DAI, because I2S signals are generated by hardware module on Zedboard.
This will instantiate sound card to change the hardware parameters for CODEC through ALSA libraries. This module can't be used to play/capture music on Zedboard with Xilinx I2S IPs.