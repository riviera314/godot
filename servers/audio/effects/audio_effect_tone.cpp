#include "audio_effect_tone.h"

void AudioEffectToneInstance::process(const AudioFrame *p_src_frames, AudioFrame *p_dst_frames, int p_frame_count) {
	float sr = AudioServer::get_singleton()->get_mix_rate();
	Vector<int> released_index;

	for (int i = 0; i < p_frame_count; i++) {
		float sample = 0.0f;
		int active_voices = 0;
		float power_sum = 0.0;

		for (Map<int, Voice>::Element *E = voices.front(); E; E = E->next()) {
			Voice &v = E->value();

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
				env = 0.7f * fmax(1.0f - v.release_time / 0.1f, 0.0f);
				if(env < 0.0001 && released_index.find(E->key(), 0) == -1){
					released_index.push_back(E->key());
				}
			}

			int index = int(v.phase * TABLE_SIZE) % TABLE_SIZE;
			float frac = fmodf(v.phase * TABLE_SIZE, 1.0f);
			int next = (index + 1) % TABLE_SIZE;
			float wave = wave_table[index] * (1.0f - frac) + wave_table[next] * frac;
			float s = wave * env;
			power_sum += s * s;
			
			sample += s;
			active_voices++;

			v.phase = fmod(v.phase + v.freq / sr, 1.0f);

			v.tone_time += 1.0f / sr;
			if (v.is_releasing) {
				v.release_time += 1.0f / sr;
			}
		}

		if (active_voices > 0) {
			float rms = sqrt(power_sum / active_voices);
			sample /= MAX(rms, 1.0);
		}

		p_dst_frames[i] = AudioFrame(tanh(sample), tanh(sample));
	}
	for(int i=0;i<released_index.size();i++){
		voices.erase(released_index[i]);
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