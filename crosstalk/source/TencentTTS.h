#pragma once

#include "ITextToSpeech.h"
#include <tencentcloud/tts/v20190823/TtsClient.h>
extern "C" {
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h"
}
#include <atomic>

using namespace TencentCloud::Tts::V20190823;

class TencentTTS : public ITextToSpeech {
public:
	struct Config : public ConfigBase {
		std::string appid;
		std::string secretid;
		std::string secretkey;
	};

	virtual int Start(ConfigBase* config, ITTSCallback* callback, std::string& language) override;
	virtual void Stop() override;
	virtual int Process(std::string& text) override;

private:
	void ProcessAudio(const uint8_t* data, int len);
	void OnResponse(const TtsClient*,
		const TencentCloud::Tts::V20190823::Model::TextToVoiceRequest&,
		TtsClient::TextToVoiceOutcome,
		const std::shared_ptr<const TencentCloud::AsyncCallerContext>&);

private:
	TtsClient* ttsClient_ = nullptr;
	std::string appid_ = "";
	std::string secretid_ = "";
	std::string secretkey_ = "";
	SwrContext* resampleContext = nullptr;
	ITTSCallback* callback_ = nullptr;
	std::string taskid_ = "";
	std::atomic_bool started_ = false;
	char audio_data_[AUDIO_10MS_DATA_LEN];
	int audio_len_ = 0;
};