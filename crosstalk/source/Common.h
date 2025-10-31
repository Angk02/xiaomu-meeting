#pragma once

#include <string>
#include "dshowcapture.hpp"
extern "C" {
#include "libavcodec/avcodec.h"
}
#include "aws/transcribestreaming/model/LanguageCode.h"

#define AUDIO_100MS_DATA_LEN (19200)
#define AUDIO_10MS_DATA_LEN (1920)
#define AUDIO_SAMPLERATE (48000)
#define AUDIO_CHANNELS (2)
#define AUDIO_10MS_SAMPLES_PER_CHANNEL (480)

enum Status
{
	STATUS_OPEN = 0,
	STATUS_STARTED = 1,
	STATUS_ERROR = 2,
	STATUS_CLOSE = 3
};

std::string WideToMulti(const std::wstring input);

std::wstring MultiToWide(const std::string input);

bool AudioFormatSupported(DShow::AudioFormat format);

AVCodecID GetAudioCodecID(DShow::AudioFormat format);

std::string GetFFmpegErrorString(int errnum);

AVSampleFormat GetSampleFormat(DShow::AudioFormat format);

int BytesPerSample(DShow::AudioFormat format);

std::string GenerateGUID();

Aws::TranscribeStreamingService::Model::LanguageCode GetLanguageCode(std::string& lang);

std::string GetLanguageBCP47(std::string& lang);

std::string GetVoiceName(std::string& lang);