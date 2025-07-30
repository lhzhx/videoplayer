#include "AudioOutput.h"
#include <iostream>
#include <cstring>
#include <alsa/asoundlib.h>

AudioOutput::AudioOutput() : initialized_(false), sample_rate_(44100), channels_(2), pcm_handle_(nullptr){}

bool AudioOutput::initialize(int sample_rate, int channels) {
    sample_rate_ = sample_rate;
    channels_ = channels;

    if (snd_pcm_open((snd_pcm_t**)&pcm_handle_, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "Failed to open ALSA device" << std::endl;
        return false;
    }
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any((snd_pcm_t*)pcm_handle_, params);
    snd_pcm_hw_params_set_access((snd_pcm_t*)pcm_handle_, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format((snd_pcm_t*)pcm_handle_, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels((snd_pcm_t*)pcm_handle_, params, channels_);
    snd_pcm_hw_params_set_rate((snd_pcm_t*)pcm_handle_, params, sample_rate_, 0);
    if (snd_pcm_hw_params((snd_pcm_t*)pcm_handle_, params) < 0) {
        std::cerr << "Failed to set ALSA parameters" << std::endl;
        return false;
    }
    initialized_ = true;
    return true;
}

void AudioOutput::play(const std::vector<float>& samples) {
    if (!initialized_) return;
    std::vector<int16_t> int_samples(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        int_samples[i] = static_cast<int16_t>(samples[i] * 32767.0f);
    }
    snd_pcm_sframes_t frames = snd_pcm_writei((snd_pcm_t*)pcm_handle_, int_samples.data(), int_samples.size() / channels_);
    if (frames < 0) {
        snd_pcm_recover((snd_pcm_t*)pcm_handle_, frames, 0);
    }
}

void AudioOutput::close() {
    if (!initialized_) return;
    if (pcm_handle_) {
        snd_pcm_close((snd_pcm_t*)pcm_handle_);
        pcm_handle_ = nullptr;
    }
    initialized_ = false;
}

AudioOutput::~AudioOutput() {
    close();
} 