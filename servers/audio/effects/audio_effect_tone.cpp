#include "audio_effect_tone.h"

void AudioEffectToneInstance::process(const AudioFrame *p_src_frames, AudioFrame *p_dst_frames, int p_frame_count) {
	float sr = AudioServer::get_singleton()->get_mix_rate();
	Vector<float> released_hz;

	for (int i = 0; i < p_frame_count; i++) {
		float sample = 0.0f;
		int active_voices = 0;

		for (int j = 0; j < voices.size(); j++) {
			Voice &v = voices.write[j];

			float env = 0.0f;
			if (!v.is_releasing) {
				if (v.tone_time < 0.01f) {
					env = v.tone_time / 0.01f;
				} else if (v.tone_time < 0.2f) {
					env = 1.0f - (v.tone_time - 0.01f) * (0.3f / 0.19f);
				} else {
					env = 0.7f;
				}
			} else {
				env = 0.7f * std::max(1.0f - v.release_time / 0.1f, 0.0f);
				if(env < 0.0001 && released_hz.find(v.freq, 0) == -1){
					released_hz.push_back(v.freq);
				}
			}

			int index = int(v.phase * TABLE_SIZE) % TABLE_SIZE;
			float frac = fmodf(v.phase * TABLE_SIZE, 1.0f);
			int next = (index + 1) % TABLE_SIZE;
			float wave = clarinet_table[index] * (1.0f - frac) + clarinet_table[next] * frac;
			float s = wave * env;
			
			sample += s;
			active_voices++;

			v.phase += v.freq / sr;
			if (v.phase >= 1.0f) {
				v.phase -= 1.0f;
			}

			v.tone_time += 1.0f / sr;
			if (v.is_releasing) {
				v.release_time += 1.0f / sr;
			}
		}

		if (active_voices > 0) {
			sample /= active_voices;
		}

		p_dst_frames[i] = AudioFrame(sample, sample);
	}
	for (int i = voices.size() - 1; i >= 0; i--) {
		if(released_hz.find(voices[i].freq, 0) != -1){
			voices.remove(i);
		}
	}
}

void AudioEffectToneInstance::generate_clarinet_table() {
	clarinet_table.resize(TABLE_SIZE);
	int harmonics = 15;

	for (int i = 0; i < TABLE_SIZE; i++) {
		float t = float(i) / TABLE_SIZE;
		float val = 0.0f;

		for (int n = 1; n <= harmonics; n += 2) {
			val += sin(2.0f * Math_PI * n * t) / powf(n, 2.0f);
		}
		clarinet_table.write[i] = val;
	}

	// ループの滑らかさのために最終サンプル = 最初にする
	clarinet_table.write[TABLE_SIZE - 1] = clarinet_table[0];
}


bool AudioEffectToneInstance::process_silence() const {
	return false;
}

void AudioEffectToneInstance::trigger_tone(float p_freq) {
	Voice v;
	v.freq = p_freq;
	v.phase = 0.0f;
	v.tone_time = 0.0f;
	v.is_releasing = false;
	v.release_time = 0.0f;
	voices.push_back(v);
}

void AudioEffectToneInstance::release_tone(float p_freq) {
	for (int i = 0; i < voices.size(); i++) {
		if(voices[i].freq == p_freq){
			Voice &v = voices.write[i];
			v.is_releasing = true;
			v.release_time = 0.0f;
		}
	}
}

void AudioEffectToneInstance::set_freq(float p_freq){
	
}

void AudioEffectToneInstance::init(){
	generate_clarinet_table();
}

Ref<AudioEffectInstance> AudioEffectTone::instance() {
	Ref<AudioEffectToneInstance> ins;
	ins.instance();
	ins->init();
	current_instance = ins;
	return ins;
}

void AudioEffectTone::_bind_methods() {
	ClassDB::bind_method(D_METHOD("trigger_tone"), &AudioEffectTone::trigger_tone);
	ClassDB::bind_method(D_METHOD("release_tone"), &AudioEffectTone::release_tone);
	ClassDB::bind_method(D_METHOD("set_freq"), &AudioEffectTone::set_freq);
	// ADD_PROPERTY(PropertyInfo(Variant::INT, "format", PROPERTY_HINT_ENUM, "8-Bit,16-Bit,IMA-ADPCM"), "set_format", "get_format");
}

AudioEffectTone::AudioEffectTone() {
	
}

AudioEffectTone::~AudioEffectTone() {
	
}

void AudioEffectTone::trigger_tone(float p_freq) {
	current_instance->trigger_tone(p_freq);
}

void AudioEffectTone::release_tone(float p_freq) {
	current_instance->release_tone(p_freq);
}

void AudioEffectTone::set_freq(float p_freq){
	current_instance->set_freq(p_freq);
}