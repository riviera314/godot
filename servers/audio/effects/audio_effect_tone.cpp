#include "audio_effect_tone.h"

void AudioEffectToneInstance::process(const AudioFrame *p_src_frames, AudioFrame *p_dst_frames, int p_frame_count) {
	float phase = base->phase;
	float freq = base->freq;
	float sr = AudioServer::get_singleton()->get_mix_rate();

	for (int i = 0; i < p_frame_count; i++) {
		float s = sinf(phase * 2.0 * Math_PI);
		p_dst_frames[i] = AudioFrame(s, s);
		phase += freq / sr;
		if (phase >= 1.0f) {
			phase -= 1.0f;
		}
	}

	base->phase = phase;
}

bool AudioEffectToneInstance::process_silence() const {
	return false;
}

Ref<AudioEffectInstance> AudioEffectTone::instance() {
	Ref<AudioEffectToneInstance> ins;
	ins.instance();
	ins->base = Ref<AudioEffectTone>(this);
	return ins;
}

void AudioEffectTone::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_freq", "p_freq"), &AudioEffectTone::set_freq);
	ClassDB::bind_method(D_METHOD("get_freq"), &AudioEffectTone::get_freq);
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "freq"), "set_freq", "get_freq");
	// ADD_PROPERTY(PropertyInfo(Variant::INT, "format", PROPERTY_HINT_ENUM, "8-Bit,16-Bit,IMA-ADPCM"), "set_format", "get_format");
}

AudioEffectTone::AudioEffectTone() {
	
}

AudioEffectTone::~AudioEffectTone() {
	
}

void AudioEffectTone::set_freq(float p_freq) {
	freq = p_freq;
}
float AudioEffectTone::get_freq() const {
	return freq;
}