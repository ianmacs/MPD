/* Adapted from NullOutputPlugin which is
 * Copyright (C) 2003-2015 The Music Player Daemon Project
 * http://www.musicpd.org
 * 
 * Copyright (C) 2015 ianmacs
 * https://github.com/ianmacs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"
#include "MulticastOutputPlugin.hxx"
#include "../OutputAPI.hxx"
#include "../Wrapper.hxx"
#include "../Timer.hxx"
#include "SoundSender.hh"

#define BILLION static_cast<const SoundSender::Clock::nsec_t>(1000000000)
#define MULTICAST_AUDIO_FORMAT SampleFormat::S16
#define MULTICAST_AUDIO_FORMAT_BYTES_PER_SAMPLE 2U
#define MULTICAST_CHANNELS 2U
#define MULTICAST_SAMPLERATE 48000U
#define MULTICAST_BLOCK_SIZE 256U

class MulticastOutput {
	friend struct AudioOutputWrapper<MulticastOutput>;

	AudioOutput base;

	SoundSender::Pacer *pacer;

	unsigned incomplete_data_bytes;
public:
	MulticastOutput()
		: base(multicast_output_plugin),
		  pacer(0),
		  incomplete_data_bytes(0)
	{}

	bool Initialize(const ConfigBlock &block, Error &error) {
		return base.Configure(block, error);
	}

	static MulticastOutput *Create(const ConfigBlock &block, Error &error);

	bool Open(AudioFormat &audio_format, gcc_unused Error &error) {
		if (audio_format.format != MULTICAST_AUDIO_FORMAT)
			return false;
		if (audio_format.channels != MULTICAST_CHANNELS)
			return false;
		if (audio_format.sample_rate != MULTICAST_SAMPLERATE)
			return false;
		SoundSender::Clock::nsec_t period =
			BILLION * MULTICAST_BLOCK_SIZE / MULTICAST_SAMPLERATE;
		pacer = new SoundSender::Pacer(SoundSender::Clock{},
					       period,
					       period/2);
		return true;
	}

	void Close() {
		delete pacer;
		pacer = 0;
	}

	unsigned Delay() const {
		return pacer->get_sleeptime() / 1000000;
	}

	size_t Play(gcc_unused const void *chunk, size_t size,
		    gcc_unused Error &error) {
		if ((size + incomplete_data_bytes) <
		    (MULTICAST_CHANNELS * MULTICAST_BLOCK_SIZE *
		     MULTICAST_AUDIO_FORMAT_BYTES_PER_SAMPLE)) {
			incomplete_data_bytes += size;
			return size;
		}
		unsigned old_incomplete_data_bytes = incomplete_data_bytes;
	        incomplete_data_bytes = 0;
		pacer->trigger();
		return MULTICAST_CHANNELS * MULTICAST_BLOCK_SIZE *
			MULTICAST_AUDIO_FORMAT_BYTES_PER_SAMPLE
			- old_incomplete_data_bytes;
	}

	void Cancel() {
	}
};

inline MulticastOutput *
MulticastOutput::Create(const ConfigBlock &block, Error &error)
{
	MulticastOutput *nd = new MulticastOutput();

	if (!nd->Initialize(block, error)) {
		delete nd;
		return nullptr;
	}

	return nd;
}

typedef AudioOutputWrapper<MulticastOutput> Wrapper;

const struct AudioOutputPlugin multicast_output_plugin = {
	"multicast",
	nullptr,
	&Wrapper::Init,
	&Wrapper::Finish,
	nullptr,
	nullptr,
	&Wrapper::Open,
	&Wrapper::Close,
	&Wrapper::Delay,
	nullptr,
	&Wrapper::Play,
	nullptr,
	&Wrapper::Cancel,
	nullptr,
	nullptr,
};

// Local Variables:
// c-basic-offset: 8
// indent-tabs-mode: t
// compile-command: "make -C ../../.."
// End:
