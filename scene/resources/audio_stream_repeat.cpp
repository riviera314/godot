/**************************************************************************/
/*  audio_stream_sample.cpp                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "audio_stream_repeat.h"

#include "core/io/marshalls.h"
#include "core/os/file_access.h"

void AudioStreamPlaybackRepeat::start(float p_from_pos) {
	if (base->format == AudioStreamRepeat::FORMAT_IMA_ADPCM) {
		//no seeking in IMA_ADPCM
		for (int i = 0; i < 2; i++) {
			ima_adpcm[i].step_index = 0;
			ima_adpcm[i].predictor = 0;
			ima_adpcm[i].loop_step_index = 0;
			ima_adpcm[i].loop_predictor = 0;
			ima_adpcm[i].last_nibble = -1;
			ima_adpcm[i].loop_pos = 0x7FFFFFFF;
			ima_adpcm[i].window_ofs = 0;
		}

		offset = 0;
	} else {
		seek(p_from_pos);
	}

	sign = 1;
	active = true;
}

void AudioStreamPlaybackRepeat::stop() {
	active = false;
}

bool AudioStreamPlaybackRepeat::is_playing() const {
	return active;
}

int AudioStreamPlaybackRepeat::get_loop_count() const {
	return 0;
}

float AudioStreamPlaybackRepeat::get_playback_position() const {
	return offset;
}
void AudioStreamPlaybackRepeat::seek(float p_time) {
	offset = p_time;
}

template <class Depth, bool is_stereo, bool is_ima_adpcm>
void AudioStreamPlaybackRepeat::do_resample(const Depth *p_src, AudioFrame *p_dst, int64_t &offset, int32_t &increment, uint32_t amount, IMA_ADPCM_State *ima_adpcm) {
	// this function will be compiled branchless by any decent compiler

	int32_t final, final_r, next, next_r;
	while (amount) {
		amount--;
		int64_t pos = offset >> MIX_FRAC_BITS;
		if (is_stereo && !is_ima_adpcm) {
			pos <<= 1;
		}

		if (is_ima_adpcm) {
			int64_t sample_pos = pos + ima_adpcm[0].window_ofs;

			while (sample_pos > ima_adpcm[0].last_nibble) {
				static const int16_t _ima_adpcm_step_table[89] = {
					7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
					19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
					50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
					130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
					337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
					876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
					2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
					5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
					15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
				};

				static const int8_t _ima_adpcm_index_table[16] = {
					-1, -1, -1, -1, 2, 4, 6, 8,
					-1, -1, -1, -1, 2, 4, 6, 8
				};

				for (int i = 0; i < (is_stereo ? 2 : 1); i++) {
					int16_t nibble, diff, step;

					ima_adpcm[i].last_nibble++;
					const uint8_t *src_ptr = (const uint8_t *)base->data;
					src_ptr += AudioStreamRepeat::DATA_PAD;

					uint8_t nbb = src_ptr[(ima_adpcm[i].last_nibble >> 1) * (is_stereo ? 2 : 1) + i];
					nibble = (ima_adpcm[i].last_nibble & 1) ? (nbb >> 4) : (nbb & 0xF);
					step = _ima_adpcm_step_table[ima_adpcm[i].step_index];

					ima_adpcm[i].step_index += _ima_adpcm_index_table[nibble];
					if (ima_adpcm[i].step_index < 0) {
						ima_adpcm[i].step_index = 0;
					}
					if (ima_adpcm[i].step_index > 88) {
						ima_adpcm[i].step_index = 88;
					}

					diff = step >> 3;
					if (nibble & 1) {
						diff += step >> 2;
					}
					if (nibble & 2) {
						diff += step >> 1;
					}
					if (nibble & 4) {
						diff += step;
					}
					if (nibble & 8) {
						diff = -diff;
					}

					ima_adpcm[i].predictor += diff;
					if (ima_adpcm[i].predictor < -0x8000) {
						ima_adpcm[i].predictor = -0x8000;
					} else if (ima_adpcm[i].predictor > 0x7FFF) {
						ima_adpcm[i].predictor = 0x7FFF;
					}

					/* store loop if there */
					if (ima_adpcm[i].last_nibble == ima_adpcm[i].loop_pos) {
						ima_adpcm[i].loop_step_index = ima_adpcm[i].step_index;
						ima_adpcm[i].loop_predictor = ima_adpcm[i].predictor;
					}

					//printf("%i - %i - pred %i\n",int(ima_adpcm[i].last_nibble),int(nibble),int(ima_adpcm[i].predictor));
				}
			}

			final = ima_adpcm[0].predictor;
			if (is_stereo) {
				final_r = ima_adpcm[1].predictor;
			}

		} else {
			final = p_src[pos];
			if (is_stereo) {
				final_r = p_src[pos + 1];
			}

			if (sizeof(Depth) == 1) { /* conditions will not exist anymore when compiled! */
				final <<= 8;
				if (is_stereo) {
					final_r <<= 8;
				}
			}

			if (is_stereo) {
				next = p_src[pos + 2];
				next_r = p_src[pos + 3];
			} else {
				next = p_src[pos + 1];
			}

			if (sizeof(Depth) == 1) {
				next <<= 8;
				if (is_stereo) {
					next_r <<= 8;
				}
			}

			int32_t frac = int64_t(offset & MIX_FRAC_MASK);

			final = final + ((next - final) * frac >> MIX_FRAC_BITS);
			if (is_stereo) {
				final_r = final_r + ((next_r - final_r) * frac >> MIX_FRAC_BITS);
			}
		}

		if (!is_stereo) {
			final_r = final; //copy to right channel if stereo
		}

		p_dst->l = final / 32767.0;
		p_dst->r = final_r / 32767.0;
		p_dst++;

		offset += increment;
	}
}

void AudioStreamPlaybackRepeat::mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) {
	if (!base->data || !active) {
		for (int i = 0; i < p_frames; i++) {
			p_buffer[i] = AudioFrame(0, 0);
		}
		return;
	}

	int len = base->data_bytes;
	switch (base->format) {
		case AudioStreamRepeat::FORMAT_8_BITS:
			len /= 1;
			break;
		case AudioStreamRepeat::FORMAT_16_BITS:
			len /= 2;
			break;
		case AudioStreamRepeat::FORMAT_IMA_ADPCM:
			len *= 2;
			break;
	}

	if (base->stereo) {
		len /= 2;
	}

	/* some 64-bit fixed point precaches */

	int64_t loop_begin_fp = ((int64_t)base->loop_begin[base->loop_index] << MIX_FRAC_BITS);
	int64_t loop_end_fp = ((int64_t)base->loop_end[base->loop_index] << MIX_FRAC_BITS);
	int64_t length_fp = ((int64_t)len << MIX_FRAC_BITS);
	int64_t begin_limit = (base->loop_mode != AudioStreamRepeat::LOOP_DISABLED) ? loop_begin_fp : 0;
	int64_t end_limit = (base->loop_mode != AudioStreamRepeat::LOOP_DISABLED) ? loop_end_fp : length_fp;
	bool is_stereo = base->stereo;

	int32_t todo = p_frames;

	if (base->loop_mode == AudioStreamRepeat::LOOP_BACKWARD) {
		sign = -1;
	}

	float global_rate_scale = AudioServer::get_singleton()->get_global_rate_scale();
	float base_rate = AudioServer::get_singleton()->get_mix_rate() * global_rate_scale;
	float srate = base->mix_rate;
	srate *= p_rate_scale;
	float fincrement = srate / base_rate;
	int32_t increment = int32_t(MAX(fincrement * MIX_FRAC_LEN, 1));
	increment *= sign;

	//looping

	AudioStreamRepeat::LoopMode loop_format = base->loop_mode;
	AudioStreamRepeat::Format format = base->format;

	/* audio data */

	uint8_t *dataptr = (uint8_t *)base->data;
	const void *data = dataptr + AudioStreamRepeat::DATA_PAD;
	AudioFrame *dst_buff = p_buffer;

	if (format == AudioStreamRepeat::FORMAT_IMA_ADPCM) {
		if (loop_format != AudioStreamRepeat::LOOP_DISABLED) {
			ima_adpcm[0].loop_pos = loop_begin_fp >> MIX_FRAC_BITS;
			ima_adpcm[1].loop_pos = loop_begin_fp >> MIX_FRAC_BITS;
			loop_format = AudioStreamRepeat::LOOP_FORWARD;
		}
	}

	while (todo > 0) {
		int64_t limit = 0;
		int32_t target = 0, aux = 0;

		/** LOOP CHECKING **/

		if (increment < 0) {
			/* going backwards */

			if (loop_format != AudioStreamRepeat::LOOP_DISABLED && offset < loop_begin_fp) {
				/* loopstart reached */
				if (loop_format == AudioStreamRepeat::LOOP_PING_PONG) {
					/* bounce ping pong */
					offset = loop_begin_fp + (loop_begin_fp - offset);
					increment = -increment;
					sign *= -1;
				} else {
					/* go to loop-end */
					offset = loop_end_fp - (loop_begin_fp - offset);
				}
			} else {
				/* check for sample not reaching beginning */
				if (offset < 0) {
					active = false;
					break;
				}
			}
		} else {
			/* going forward */
			if (loop_format != AudioStreamRepeat::LOOP_DISABLED && offset >= loop_end_fp) {
				base->loop_index = (base->loop_index + 1)%base->loop_end.size();
				loop_begin_fp = ((int64_t)base->loop_begin[base->loop_index] << MIX_FRAC_BITS);
				/* loopend reached */

				if (loop_format == AudioStreamRepeat::LOOP_PING_PONG) {
					/* bounce ping pong */
					offset = loop_end_fp - (offset - loop_end_fp);
					increment = -increment;
					sign *= -1;
				} else {
					/* go to loop-begin */

					if (format == AudioStreamRepeat::FORMAT_IMA_ADPCM) {
						for (int i = 0; i < 2; i++) {
							ima_adpcm[i].step_index = ima_adpcm[i].loop_step_index;
							ima_adpcm[i].predictor = ima_adpcm[i].loop_predictor;
							ima_adpcm[i].last_nibble = loop_begin_fp >> MIX_FRAC_BITS;
						}
						offset = loop_begin_fp;
					} else {
						offset = loop_begin_fp + (offset - loop_end_fp);
					}
				}
				loop_end_fp = ((int64_t)base->loop_end[base->loop_index] << MIX_FRAC_BITS);

				begin_limit = (base->loop_mode != AudioStreamRepeat::LOOP_DISABLED) ? loop_begin_fp : 0;
				end_limit = (base->loop_mode != AudioStreamRepeat::LOOP_DISABLED) ? loop_end_fp : length_fp;
			} else {
				/* no loop, check for end of sample */
				if (offset >= length_fp) {
					active = false;
					break;
				}
			}
		}

		/** MIXCOUNT COMPUTING **/

		/* next possible limit (looppoints or sample begin/end */
		limit = (increment < 0) ? begin_limit : end_limit;

		/* compute what is shorter, the todo or the limit? */
		aux = (limit - offset) / increment + 1;
		target = (aux < todo) ? aux : todo; /* mix target is the shorter buffer */

		/* check just in case */
		if (target <= 0) {
			active = false;
			break;
		}

		todo -= target;

		switch (base->format) {
			case AudioStreamRepeat::FORMAT_8_BITS: {
				if (is_stereo) {
					do_resample<int8_t, true, false>((int8_t *)data, dst_buff, offset, increment, target, ima_adpcm);
				} else {
					do_resample<int8_t, false, false>((int8_t *)data, dst_buff, offset, increment, target, ima_adpcm);
				}
			} break;
			case AudioStreamRepeat::FORMAT_16_BITS: {
				if (is_stereo) {
					do_resample<int16_t, true, false>((int16_t *)data, dst_buff, offset, increment, target, ima_adpcm);
				} else {
					do_resample<int16_t, false, false>((int16_t *)data, dst_buff, offset, increment, target, ima_adpcm);
				}

			} break;
			case AudioStreamRepeat::FORMAT_IMA_ADPCM: {
				if (is_stereo) {
					do_resample<int8_t, true, true>((int8_t *)data, dst_buff, offset, increment, target, ima_adpcm);
				} else {
					do_resample<int8_t, false, true>((int8_t *)data, dst_buff, offset, increment, target, ima_adpcm);
				}

			} break;
		}

		dst_buff += target;
	}

	if (todo) {
		//bit was missing from mix
		int todo_ofs = p_frames - todo;
		for (int i = todo_ofs; i < p_frames; i++) {
			p_buffer[i] = AudioFrame(0, 0);
		}
	}
}

AudioStreamPlaybackRepeat::AudioStreamPlaybackRepeat() {
	active = false;
	offset = 0;
	sign = 1;
}

/////////////////////

void AudioStreamRepeat::set_format(Format p_format) {
	format = p_format;
}

AudioStreamRepeat::Format AudioStreamRepeat::get_format() const {
	return format;
}

void AudioStreamRepeat::set_loop_mode(LoopMode p_loop_mode) {
	loop_mode = p_loop_mode;
}
AudioStreamRepeat::LoopMode AudioStreamRepeat::get_loop_mode() const {
	return loop_mode;
}

void AudioStreamRepeat::set_loop_begin(Vector<int> p_frame) {
	loop_begin = p_frame;
}
Vector<int> AudioStreamRepeat::get_loop_begin() const {
	return loop_begin;
}

void AudioStreamRepeat::set_loop_end(Vector<int> p_frame) {
	loop_end = p_frame;
	if(loop_index >= p_frame.size()){
		loop_index -= (loop_index - p_frame.size())+1;
	}
}
Vector<int> AudioStreamRepeat::get_loop_end() const {
	return loop_end;
}

void AudioStreamRepeat::set_mix_rate(int p_hz) {
	ERR_FAIL_COND(p_hz == 0);
	mix_rate = p_hz;
}
int AudioStreamRepeat::get_mix_rate() const {
	return mix_rate;
}
void AudioStreamRepeat::set_stereo(bool p_enable) {
	stereo = p_enable;
}
bool AudioStreamRepeat::is_stereo() const {
	return stereo;
}

float AudioStreamRepeat::get_length() const {
	int len = data_bytes;
	switch (format) {
		case AudioStreamRepeat::FORMAT_8_BITS:
			len /= 1;
			break;
		case AudioStreamRepeat::FORMAT_16_BITS:
			len /= 2;
			break;
		case AudioStreamRepeat::FORMAT_IMA_ADPCM:
			len *= 2;
			break;
	}

	if (stereo) {
		len /= 2;
	}

	return float(len) / mix_rate;
}

void AudioStreamRepeat::set_data(const PoolVector<uint8_t> &p_data) {
	AudioServer::get_singleton()->lock();
	if (data) {
		AudioServer::get_singleton()->audio_data_free(data);
		data = nullptr;
		data_bytes = 0;
	}

	int datalen = p_data.size();
	if (datalen) {
		PoolVector<uint8_t>::Read r = p_data.read();
		int alloc_len = datalen + DATA_PAD * 2;
		data = AudioServer::get_singleton()->audio_data_alloc(alloc_len); //alloc with some padding for interpolation
		memset(data, 0, alloc_len);
		uint8_t *dataptr = (uint8_t *)data;
		memcpy(dataptr + DATA_PAD, r.ptr(), datalen);
		data_bytes = datalen;
	}

	AudioServer::get_singleton()->unlock();
}
PoolVector<uint8_t> AudioStreamRepeat::get_data() const {
	PoolVector<uint8_t> pv;

	if (data) {
		pv.resize(data_bytes);
		{
			PoolVector<uint8_t>::Write w = pv.write();
			uint8_t *dataptr = (uint8_t *)data;
			memcpy(w.ptr(), dataptr + DATA_PAD, data_bytes);
		}
	}

	return pv;
}

Error AudioStreamRepeat::save_to_wav(const String &p_path) {
	if (format == AudioStreamRepeat::FORMAT_IMA_ADPCM) {
		WARN_PRINT("Saving IMA_ADPC samples are not supported yet");
		return ERR_UNAVAILABLE;
	}

	int sub_chunk_2_size = data_bytes; //Subchunk2Size = Size of data in bytes

	// Format code
	// 1:PCM format (for 8 or 16 bit)
	// 3:IEEE float format
	int format_code = (format == FORMAT_IMA_ADPCM) ? 3 : 1;

	int n_channels = stereo ? 2 : 1;

	long sample_rate = mix_rate;

	int byte_pr_sample = 0;
	switch (format) {
		case AudioStreamRepeat::FORMAT_8_BITS:
			byte_pr_sample = 1;
			break;
		case AudioStreamRepeat::FORMAT_16_BITS:
			byte_pr_sample = 2;
			break;
		case AudioStreamRepeat::FORMAT_IMA_ADPCM:
			byte_pr_sample = 4;
			break;
	}

	String file_path = p_path;
	if (!(file_path.substr(file_path.length() - 4, 4) == ".wav")) {
		file_path += ".wav";
	}

	FileAccessRef file = FileAccess::open(file_path, FileAccess::WRITE); //Overrides existing file if present

	ERR_FAIL_COND_V(!file, ERR_FILE_CANT_WRITE);

	// Create WAV Header
	file->store_string("RIFF"); //ChunkID
	file->store_32(sub_chunk_2_size + 36); //ChunkSize = 36 + SubChunk2Size (size of entire file minus the 8 bits for this and previous header)
	file->store_string("WAVE"); //Format
	file->store_string("fmt "); //Subchunk1ID
	file->store_32(16); //Subchunk1Size = 16
	file->store_16(format_code); //AudioFormat
	file->store_16(n_channels); //Number of Channels
	file->store_32(sample_rate); //SampleRate
	file->store_32(sample_rate * n_channels * byte_pr_sample); //ByteRate
	file->store_16(n_channels * byte_pr_sample); //BlockAlign = NumChannels * BytePrSample
	file->store_16(byte_pr_sample * 8); //BitsPerSample
	file->store_string("data"); //Subchunk2ID
	file->store_32(sub_chunk_2_size); //Subchunk2Size

	// Add data
	PoolVector<uint8_t> data = get_data();
	PoolVector<uint8_t>::Read read_data = data.read();
	switch (format) {
		case AudioStreamRepeat::FORMAT_8_BITS:
			for (unsigned int i = 0; i < data_bytes; i++) {
				uint8_t data_point = (read_data[i] + 128);
				file->store_8(data_point);
			}
			break;
		case AudioStreamRepeat::FORMAT_16_BITS:
			for (unsigned int i = 0; i < data_bytes / 2; i++) {
				uint16_t data_point = decode_uint16(&read_data[i * 2]);
				file->store_16(data_point);
			}
			break;
		case AudioStreamRepeat::FORMAT_IMA_ADPCM:
			//Unimplemented
			break;
	}

	file->close();

	return OK;
}

int AudioStreamRepeat::get_loop_index() const {
	return loop_index;
}

void AudioStreamRepeat::set_loop_index(int index) {
	loop_index = index;
}

Ref<AudioStreamPlayback> AudioStreamRepeat::instance_playback() {
	Ref<AudioStreamPlaybackRepeat> sample;
	sample.instance();
	sample->base = Ref<AudioStreamRepeat>(this);
	return sample;
}

String AudioStreamRepeat::get_stream_name() const {
	return "";
}

void AudioStreamRepeat::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_data", "data"), &AudioStreamRepeat::set_data);
	ClassDB::bind_method(D_METHOD("get_data"), &AudioStreamRepeat::get_data);

	ClassDB::bind_method(D_METHOD("set_format", "format"), &AudioStreamRepeat::set_format);
	ClassDB::bind_method(D_METHOD("get_format"), &AudioStreamRepeat::get_format);

	ClassDB::bind_method(D_METHOD("set_loop_mode", "loop_mode"), &AudioStreamRepeat::set_loop_mode);
	ClassDB::bind_method(D_METHOD("get_loop_mode"), &AudioStreamRepeat::get_loop_mode);

	ClassDB::bind_method(D_METHOD("set_loop_begin", "loop_begin"), &AudioStreamRepeat::set_loop_begin);
	ClassDB::bind_method(D_METHOD("get_loop_begin"), &AudioStreamRepeat::get_loop_begin);

	ClassDB::bind_method(D_METHOD("set_loop_end", "loop_end"), &AudioStreamRepeat::set_loop_end);
	ClassDB::bind_method(D_METHOD("get_loop_end"), &AudioStreamRepeat::get_loop_end);

	ClassDB::bind_method(D_METHOD("set_mix_rate", "mix_rate"), &AudioStreamRepeat::set_mix_rate);
	ClassDB::bind_method(D_METHOD("get_mix_rate"), &AudioStreamRepeat::get_mix_rate);

	ClassDB::bind_method(D_METHOD("set_stereo", "stereo"), &AudioStreamRepeat::set_stereo);
	ClassDB::bind_method(D_METHOD("is_stereo"), &AudioStreamRepeat::is_stereo);

	ClassDB::bind_method(D_METHOD("save_to_wav", "path"), &AudioStreamRepeat::save_to_wav);

	ClassDB::bind_method(D_METHOD("get_loop_index"), &AudioStreamRepeat::get_loop_index);
	ClassDB::bind_method(D_METHOD("set_loop_index", "index"), &AudioStreamRepeat::set_loop_index);

	ADD_PROPERTY(PropertyInfo(Variant::POOL_BYTE_ARRAY, "data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR), "set_data", "get_data");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "format", PROPERTY_HINT_ENUM, "8-Bit,16-Bit,IMA-ADPCM"), "set_format", "get_format");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "loop_mode", PROPERTY_HINT_ENUM, "Disabled,Forward,Ping-Pong,Backward"), "set_loop_mode", "get_loop_mode");
	ADD_PROPERTY(PropertyInfo(Variant::POOL_INT_ARRAY, "loop_begin"), "set_loop_begin", "get_loop_begin");
	ADD_PROPERTY(PropertyInfo(Variant::POOL_INT_ARRAY, "loop_end"), "set_loop_end", "get_loop_end");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mix_rate"), "set_mix_rate", "get_mix_rate");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "stereo"), "set_stereo", "is_stereo");

	BIND_ENUM_CONSTANT(FORMAT_8_BITS);
	BIND_ENUM_CONSTANT(FORMAT_16_BITS);
	BIND_ENUM_CONSTANT(FORMAT_IMA_ADPCM);

	BIND_ENUM_CONSTANT(LOOP_DISABLED);
	BIND_ENUM_CONSTANT(LOOP_FORWARD);
	BIND_ENUM_CONSTANT(LOOP_PING_PONG);
	BIND_ENUM_CONSTANT(LOOP_BACKWARD);
}

AudioStreamRepeat::AudioStreamRepeat() {
	Vector<int> loop_array;
	loop_array.push_back(0);

	format = FORMAT_8_BITS;
	loop_mode = LOOP_DISABLED;
	stereo = false;
	loop_begin = loop_array;
	loop_end = loop_array;
	mix_rate = 44100;
	data = nullptr;
	data_bytes = 0;
	loop_index = 0;
}

AudioStreamRepeat::~AudioStreamRepeat() {
	if (data) {
		AudioServer::get_singleton()->audio_data_free(data);
		data = nullptr;
		data_bytes = 0;
	}
}
