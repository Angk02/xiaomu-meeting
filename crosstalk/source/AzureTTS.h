#pragma once

#include "ITextToSpeech.h"
#include "speechapi_cxx.h"
extern "C" {
#include "libswresample/swresample.h"
}
#include <thread>
#include <list>
#include <mutex>
#include <condition_variable>

using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Audio;

class AzureTTS : public ITextToSpeech {
public:
	struct Config : public ConfigBase {
		std::string secretkey;
		std::string region;
	};

	int Start(ConfigBase* config, ITTSCallback* callback, std::string& language) override;

	void Stop() override;

	int Process(std::string& text) override;

private:
	void worker();
	void ProcessAudio(const uint8_t* audio, size_t len);

private:
	std::shared_ptr<SpeechSynthesizer> synthesizer_ = nullptr;
	std::shared_ptr<Connection> connection_;

	std::string secretkey_ = "";
	std::string region_ = "";
	ITTSCallback* callback_ = nullptr;
	std::atomic_bool started_ = false;
	std::string target_language_ = "ch";
	uint8_t audio_data_[960];
	size_t audio_len_ = 960;
	size_t audio_current_len_ = 0;
	bool run_ = false;
	std::thread work_thread_;
	std::list<std::string> texts_;
	std::mutex mutex_;
	std::condition_variable condition_;
	SwrContext* resampleContext = nullptr;
};