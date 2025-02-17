#pragma once
#include "pch.h"
#include <vector>

extern "C" {
#include <Limelight.h>
#include <opus/opus_multistream.h>
#include "third_party/miniaudio.h"
}

namespace moonlight_xbox_dx
{
	class AudioPlayer {
	public:
		int Init(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION opusConfig, void* context, int arFlags);
		void Start();
		void Stop();
		void Cleanup();
		int SubmitDU(char* sampleData, int sampleLength);
		static AUDIO_RENDERER_CALLBACKS getDecoder();
		ma_pcm_rb m_rb;
	private:
		OpusMSDecoder* m_decoder;
		int m_sampleRate;
		size_t dropCount{};
		ma_device m_device;

		std::vector<opus_int16> m_pcmBuffer;
	};
}