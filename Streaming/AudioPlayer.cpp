#include "pch.h"
#include <sstream>
#include <memory>

#include <opus/opus_multistream.h>
#include "State/MoonlightClient.h"
#include "Utils.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include "Streaming/AudioPlayer.h"

// this will give a maximum latency of FRAME_SIZE * PERIODS * 1000 / sampleRate + MAX_PENDING_DURATION
static constexpr size_t FRAME_SIZE = 240;
static constexpr size_t PERIODS = 3;
static constexpr size_t MAX_PENDING_DURATION = 20; // in ms

namespace moonlight_xbox_dx {
	//Helpers
	std::shared_ptr<AudioPlayer> audioPlayerInstance;
	ma_context context;
	bool contexted = false;

	int audioInitCallback(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION opusConfig, void* context, int arFlags) {
		return audioPlayerInstance->Init(audioConfiguration,opusConfig, context, arFlags);
	}
	void audioStartCallback() {
		audioPlayerInstance->Start();
	}
	void audioStopCallback() {
		audioPlayerInstance->Stop();
	}
	void audioCleanupCallback() {
		audioPlayerInstance->Cleanup();
		audioPlayerInstance.reset();
	}
	void audioSubmitCallback(char* sampleData, int sampleLength) {
		audioPlayerInstance->SubmitDU(sampleData,sampleLength);
	}

	AUDIO_RENDERER_CALLBACKS AudioPlayer::getDecoder() {
		if (audioPlayerInstance == nullptr) {
			audioPlayerInstance = std::make_shared<AudioPlayer>();
		}

		AUDIO_RENDERER_CALLBACKS decoder_callbacks_sdl;
		LiInitializeAudioCallbacks(&decoder_callbacks_sdl);
		decoder_callbacks_sdl.init = audioInitCallback;
		decoder_callbacks_sdl.start = audioStartCallback;
		decoder_callbacks_sdl.stop = audioStopCallback;
		decoder_callbacks_sdl.cleanup = audioCleanupCallback;
		decoder_callbacks_sdl.decodeAndPlaySample = audioSubmitCallback;
		decoder_callbacks_sdl.capabilities = CAPABILITY_DIRECT_SUBMIT;
		return decoder_callbacks_sdl;
	}

	void requireAudioData(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
	{
		void* buffer;
		ma_uint32 len = frameCount;
		ma_result res = ma_pcm_rb_acquire_read(&audioPlayerInstance->m_rb, &len, &buffer);
		if (res != MA_SUCCESS) {
			Utils::Log("Failed to read audio data\n");
			return;
		}
		if (len > 0) {
			memcpy(pOutput, buffer, static_cast<size_t>(len) * ma_pcm_rb_get_bpf(&audioPlayerInstance->m_rb));
			res = ma_pcm_rb_commit_read(&audioPlayerInstance->m_rb, len, buffer);
			if (res != MA_SUCCESS && res != MA_AT_END) {
				Utils::Log("Failed to read audio data to shared buffer\n");
				return;
			}
		}
	}

	int AudioPlayer::Init(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION opusConfig, void* mnlContext, int arFlags) {
		HRESULT hr{};
		int rc;
		m_decoder = opus_multistream_decoder_create(opusConfig->sampleRate, opusConfig->channelCount, opusConfig->streams, opusConfig->coupledStreams, opusConfig->mapping, &rc);
		if (rc != 0) {
			return rc;
		}
		if (!contexted) {
			contexted = true;
			std::array<ma_backend, 1> backends{ ma_backend_wasapi };
			if (ma_context_init(backends.data(), 1, nullptr, &context) != MA_SUCCESS) {
				Utils::Log("Failed to open playback device. Context creation failed.\n");
				return -3;
			}
		}

		ma_device_config deviceConfig;
		deviceConfig = ma_device_config_init(ma_device_type_playback);
		deviceConfig.playback.format = ma_format_s16;
		deviceConfig.playback.channels = opusConfig->channelCount;
		deviceConfig.sampleRate = opusConfig->sampleRate;
		deviceConfig.periods = PERIODS;
		deviceConfig.periodSizeInFrames = FRAME_SIZE;
		deviceConfig.wasapi.noAutoConvertSRC = true;
		deviceConfig.dataCallback = requireAudioData;

		if (ma_device_init(&context, &deviceConfig, &m_device) != MA_SUCCESS) {
			Utils::Log("Failed to open playback device.\n");
			return -3;
		}

		ma_result r = ma_pcm_rb_init(ma_format_s16, opusConfig->channelCount, FRAME_SIZE * 10, nullptr, nullptr, &this->m_rb);
		if (r != MA_SUCCESS) {
			Utils::Log("Failed to create shared buffer\n");
		}

		this->m_sampleRate = opusConfig->sampleRate;
		this->m_pcmBuffer.resize(FRAME_SIZE * opusConfig->channelCount);

		return r;
	}

	void AudioPlayer::Cleanup() {
		Utils::Log("Audio Cleanup\n");
		if (m_decoder != nullptr) {
			opus_multistream_decoder_destroy(m_decoder);
		}
		ma_device_stop(&m_device);
		ma_device_uninit(&m_device);
		ma_pcm_rb_uninit(&m_rb);

		// Crashes due to "Cannot change thread mode after it is set"
		//ma_context_uninit(&context);
	}

	int AudioPlayer::SubmitDU(char* sampleData, int sampleLength) {
		int decodeLen = opus_multistream_decode(m_decoder, reinterpret_cast<unsigned char*>(sampleData), sampleLength, m_pcmBuffer.data(), FRAME_SIZE, 0);
		if (decodeLen > 0) {
			// in ms
			auto pendingDuration = LiGetPendingAudioDuration() + ma_pcm_rb_available_read(&m_rb) * 1000 / this->m_sampleRate;
			if (pendingDuration < MAX_PENDING_DURATION) {
				void* buffer;
				ma_uint32 len = decodeLen;
				ma_result r = ma_pcm_rb_acquire_write(&m_rb, &len, &buffer);
				if (r != MA_SUCCESS) {
					Utils::Log("Failed to acquire shared buffer\n");
					return -1;
				}
				memcpy(buffer, m_pcmBuffer.data(), static_cast<size_t>(len) * ma_pcm_rb_get_bpf(&m_rb));
				r = ma_pcm_rb_commit_write(&m_rb, len, buffer);
				if (r != MA_SUCCESS) {
					Utils::Log("Failed to write to shared buffer\n");
					return -1;
				}
			}
			else {
				++this->dropCount;

				std::stringstream ss;
				ss << "Dropping audio packet. Total dropped: " << this->dropCount << std::endl;
				Utils::Log(ss.str().c_str());
			}
		}
		else {
		}
		return 0;
	}

	void AudioPlayer::Start() {
		if (ma_device_start(&m_device) != MA_SUCCESS) {
			Utils::Log("Failed to start playback m_device.\n");
			ma_device_uninit(&m_device);
		}
	}

	void AudioPlayer::Stop() {
		if (ma_device_stop(&m_device) != MA_SUCCESS) {
			Utils::Log("Failed to start playback m_device.\n");
			ma_device_uninit(&m_device);
		}
	}
}
