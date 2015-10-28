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

class MulticastOutput {
	friend struct AudioOutputWrapper<MulticastOutput>;

	AudioOutput base;

	bool sync;

	Timer *timer;

public:
	MulticastOutput()
		:base(multicast_output_plugin) {}

	bool Initialize(const ConfigBlock &block, Error &error) {
		return base.Configure(block, error);
	}

	static MulticastOutput *Create(const ConfigBlock &block, Error &error);

	bool Open(AudioFormat &audio_format, gcc_unused Error &error) {
		if (sync)
			timer = new Timer(audio_format);

		return true;
	}

	void Close() {
		if (sync)
			delete timer;
	}

	unsigned Delay() const {
		return sync && timer->IsStarted()
			? timer->GetDelay()
			: 0;
	}

	size_t Play(gcc_unused const void *chunk, size_t size,
		    gcc_unused Error &error) {
		if (sync) {
			if (!timer->IsStarted())
				timer->Start();
			timer->Add(size);
		}

		return size;
	}

	void Cancel() {
		if (sync)
			timer->Reset();
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

	nd->sync = block.GetBlockValue("sync", true);

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
