#pragma once

#include "IRecognition.h"
#include "aws/transcribestreaming/TranscribeStreamingServiceClient.h"
#include "aws/transcribestreaming/model/StartStreamTranscriptionRequest.h"
#include <thread>
#include <atomic>

using namespace Aws::TranscribeStreamingService;

class AWSRecognition : public IRecognition {
public:

	struct Config : public ConfigBase {
		std::string secretid;
		std::string secretkey;
	};

	// 通过 IRecognition 继承
	int Start(ConfigBase* config, IRecCallback* callback, std::string& sourceLanguage) override;

	void Stop() override;

	int Process(const uint8_t* audio, const size_t len, int samplerate, int channels) override;

private:
	void OnStreamReady(Aws::TranscribeStreamingService::Model::AudioStream& stream);
	void OnResponse(
		const TranscribeStreamingServiceClient* ,
		const Aws::TranscribeStreamingService::Model::StartStreamTranscriptionRequest& ,
		const Aws::TranscribeStreamingService::Model::StartStreamTranscriptionOutcome& outcome,
		const std::shared_ptr<const Aws::Client::AsyncCallerContext>&);

private:
	TranscribeStreamingServiceClient* transcribe_client_ = nullptr;
	Aws::TranscribeStreamingService::Model::StartStreamTranscriptionHandler transcribe_handler_;
	Aws::TranscribeStreamingService::Model::StartStreamTranscriptionRequest transcribe_request_;
	std::thread worker_thread_;
	bool run_ = false;
	std::string secretid_ = "";
	std::string secretkey_ = "";
	std::string source_language_ = "zh";
	IRecCallback* callback_ = nullptr;
	int current_audio_len_ = 0;
	std::mutex mutex_;
	std::condition_variable condition_;
	char audio_data_[AUDIO_10MS_DATA_LEN * 10];
	int audio_data_len_ = AUDIO_10MS_DATA_LEN * 5;
	std::atomic_bool ready_ = false;
};