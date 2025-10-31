#pragma once

#include "IRecognition.h"
#include "speechapi_cxx.h"
extern "C" {
#include "libswresample/swresample.h"
}

using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Audio;

class AzureRecognition : public IRecognition {
public:
	struct Config : public ConfigBase {
		std::string secretkey;
	};

	int Start(ConfigBase* config, IRecCallback* callback, std::string& sourceLanguage) override;

	void Stop() override;

	int Process(const uint8_t* audio, const size_t len, int samplerate, int channels) override;

private:
	std::shared_ptr<SpeechRecognizer> recognizer_ = nullptr;
	std::shared_ptr<AudioStreamFormat> format_ = nullptr;
	std::shared_ptr<PushAudioInputStream> stream_ = nullptr;
	std::shared_ptr<AudioConfig> audio_config_ = nullptr;
	std::shared_ptr<SpeechConfig> speech_config_ = nullptr;
	std::string secretkey_ = "";
	std::string source_language_ = "zh";
	IRecCallback* callback_ = nullptr;
	char audio_data_[AUDIO_10MS_DATA_LEN * 10];
	int audio_data_len_ = AUDIO_10MS_DATA_LEN * 10;
	int current_audio_len_ = 0;
	SwrContext* resampleContext_ = nullptr;
};