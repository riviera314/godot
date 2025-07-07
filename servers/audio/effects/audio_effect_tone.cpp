#include "audio_effect_tone.h"

void AudioEffectToneInstance::process(const AudioFrame *p_src_frames, AudioFrame *p_dst_frames, int p_frame_count) {
	float sr = AudioServer::get_singleton()->get_mix_rate();
	float inv_sr = 1.0f / sr;

	Set<int> released_index;

	for (int i = 0; i < p_frame_count; i++) {
		float sample = 0.0f;
		float active_voices = 0;
		int count = 0;
		int count2 = 0;

		for (Map<int, Voice>::Element *E = voices.front(); E; E = E->next()) {
			Voice &v = E->value();

			float env;
			if (!v.is_releasing) {
				if (v.tone_time < 0.01f)
					env = v.tone_time * 100.0f;
				else if (v.tone_time < 0.2f)
					env = 1.0f - (v.tone_time - 0.01f) * (0.3f / 0.19f);
				else
					env = 0.7f;
				count++;
			} else {
				env = 0.7f * expf(-v.release_time * 30.0f);
				if (env < 0.00001f)
					released_index.insert(E->key());
				count2++;
			}

			int idx = int(v.phase * TABLE_SIZE) & (TABLE_SIZE - 1); // TABLE_SIZEが2のべきなら高速
			int next = (idx + 1) & (TABLE_SIZE - 1);
			float frac = fmodf(v.phase * TABLE_SIZE, 1.0f);

			float wave = wave_table[idx] * (1.0f - frac) + wave_table[next] * frac;
			sample += wave * env;

			active_voices += env;
			v.phase = fmodf(v.phase + v.freq * inv_sr, 1.0f);
			v.tone_time += inv_sr;
			if (v.is_releasing)
				v.release_time += inv_sr;
		}

		if (count > 0) {
			sample *= 0.6f / float(active_voices);
		}
		else{
			sample *= 0.6f / (0.7f*count2);
		}

		p_dst_frames[i] = AudioFrame(sample, sample);
	}

	for (Set<int>::Element *E = released_index.front(); E; E = E->next()) {
		voices.erase(E->get());
	}
}


void AudioEffectToneInstance::generate_wave_table(InstrumentType type) {
	wave_table.resize(TABLE_SIZE);
	for (int i = 0; i < TABLE_SIZE; i++) {
		float t = float(i) / TABLE_SIZE;
		float val = 0.0f;

		switch (type) {
			case INSTRUMENT_SINE:
				for (int i = 0; i < TABLE_SIZE; i++) {
					float t = float(i) / TABLE_SIZE;
					val = sin(2.0f * Math_PI * t); // 基本のサイン波
					wave_table.write[i] = val;
				}
				break;
			case INSTRUMENT_CLARINET:
				for (int n = 1; n <= 15; n += 2)
					val += sin(2.0f * Math_PI * n * t) / powf(n, 2.0f);
				break;
			case INSTRUMENT_SAX:
				for (int n = 1; n <= 15; n++)
					val += sin(2.0f * Math_PI * n * t) / powf(n, 1.8f);
				break;
			case INSTRUMENT_FLUTE:
				val += sin(2.0f * Math_PI * t);
				val += 0.1f * sin(2.0f * Math_PI * 2 * t);
				val += 0.05f * sin(2.0f * Math_PI * 3 * t);
				break;
			case INSTRUMENT_TRUMPET:
				for (int n = 1; n <= 20; n++)
					val += sin(2.0f * Math_PI * n * t) / powf(n, 1.2f);
				break;
		}

		wave_table.write[i] = val;
	}
	wave_table.write[TABLE_SIZE - 1] = wave_table[0]; // wrap
}

bool AudioEffectToneInstance::process_silence() const {
	return false;
}

void AudioEffectToneInstance::trigger_tone(int index, float p_freq){
	if (voices.has(index)) {
		Voice &v = voices[index];
		if (v.is_releasing) {
			// 途中で復活させる
			v.is_releasing = false;
			v.release_time = 0.0f;
			v.tone_time = 0.0f;
			v.freq = p_freq;
			return;
		}
	}

	Voice v;
	v.freq = p_freq;
	v.phase = Math::randf();
	v.tone_time = 0.0f;
	v.is_releasing = false;
	v.release_time = 0.0f;
	voices[index] = v;
}
void AudioEffectToneInstance::release_tone(int index){
	voices[index].is_releasing = true;
	voices[index].release_time = 0.0f;
}
void AudioEffectToneInstance::change_freq(int index, float p_freq){
	voices[index].freq = p_freq;
}

void AudioEffectToneInstance::init(){
	
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
	ClassDB::bind_method(D_METHOD("change_freq"), &AudioEffectTone::change_freq);
	ClassDB::bind_method(D_METHOD("set_instrument"), &AudioEffectTone::set_instrument);
	// ADD_PROPERTY(PropertyInfo(Variant::INT, "format", PROPERTY_HINT_ENUM, "8-Bit,16-Bit,IMA-ADPCM"), "set_format", "get_format");
}

AudioEffectTone::AudioEffectTone() {
	
}

AudioEffectTone::~AudioEffectTone() {
	
}

void AudioEffectTone::trigger_tone(int index, float p_freq) {
	current_instance->trigger_tone(index, p_freq);
}

void AudioEffectTone::release_tone(int index) {
	current_instance->release_tone(index);
}

void AudioEffectTone::change_freq(int index, float p_freq){
	current_instance->change_freq(index, p_freq);
}

void AudioEffectTone::set_instrument(int type) {
	current_instance->generate_wave_table((AudioEffectToneInstance::InstrumentType)type);
}