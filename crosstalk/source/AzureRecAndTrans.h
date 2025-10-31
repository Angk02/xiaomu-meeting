#pragma once

#include "IRecAndTrans.h"
#include "speechapi_cxx.h"
extern "C" {
#include "libswresample/swresample.h"
}
#include <thread>
#include <mutex>
#include <condition_variable>

using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Audio;
using namespace Microsoft::CognitiveServices::Speech::Translation;

class AzureRecAndTrans : public IRecAndTrans {
public:
	struct Config : public ConfigBase {
		std::string secretkey;
		std::string region;
	};

	int Start(ITranslation::ConfigBase* config, ITransCallback* callback, std::string& sourceLanguage, std::string targetLanguage) override;
	
	void Stop() override;

	int Process(const uint8_t* audio, const size_t len, int samplerate, int channels) override;

private:
	void worker();

private:
	std::shared_ptr<SpeechTranslationConfig> speech_config_ = nullptr;
	std::shared_ptr<AudioStreamFormat> format_ = nullptr;
	std::shared_ptr<PushAudioInputStream> stream_ = nullptr;
	std::shared_ptr<AudioConfig> audio_config_ = nullptr;
	std::shared_ptr<TranslationRecognizer> recognizer_ = nullptr;
	std::string secretkey_ = "";
	std::string region_ = "";
	std::string source_language_ = "zh";
	std::string target_language_ = "en";
	ITransCallback* callback_ = nullptr;
	char audio_data_[AUDIO_10MS_DATA_LEN * 10];
	int audio_data_len_ = AUDIO_10MS_DATA_LEN * 10;
	int current_audio_len_ = 0;
	SwrContext* resampleContext_ = nullptr;
	bool run_ = false;
	std::thread work_thread_;
	std::mutex mutex_;
	std::condition_variable condition_;
};