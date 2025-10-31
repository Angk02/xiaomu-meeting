#pragma once

#include "dshowcapture.hpp"
#include "Common.h"
#include "WASAPIRenderer.h"
extern "C" {
	#include "libavcodec/avcodec.h"
	#include "libswresample/swresample.h"
}
#include <string>

class AudioCaptrue {
public:
	class IAudioSink {
	public:
		virtual ~IAudioSink() {}
		virtual void OnAudioFrame(const uint8_t* audio, const size_t len, int samplerate, int channels) = 0;
	};

	int Init(std::string& friendlyname, std::string& uniquename, IAudioSink* sink);
	void Release();
	int Start();
	void Stop();
	void SetPlayoutDevice(std::string& deviceName);

private:

	void OnAudioFrame(const DShow::AudioConfig& config, unsigned char* data,
		size_t size, long long startTime, long long stopTime);

	void OnAudioDecodedFrame(uint8_t* data, int samples, AVChannelLayout* ch_layout, AVSampleFormat fmt, int sample_rate);

private:
	DShow::Device* dshowDevice = nullptr;
	DShow::Config* dshowConfig = nullptr;

	AVFrame* frame = nullptr;
	const AVCodec* avCodec = nullptr;
	AVCodecContext* avCodecContext = nullptr;

	//audio
	SwrContext* resampleContext = nullptr;
	uint8_t resampleData[AUDIO_100MS_DATA_LEN]; //48KHz stereo 100ms data
	int dataOffset = 0;
	IAudioSink* sink_ = nullptr;
	std::string playout_device_name_ = "";
	CWASAPIRenderer* render_ = nullptr;
	LoopbackBuffer* buffer_ = nullptr;
};