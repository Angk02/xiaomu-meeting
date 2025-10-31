#include "AWSRecognition.h"
#include "aws/core/auth/AWSCredentials.h"
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/utils/logging/LogMacros.h>
#include <aws/transcribestreaming/TranscribeStreamingServiceClient.h>
#include <aws/transcribestreaming/model/StartStreamTranscriptionRequest.h>
#include <aws/core/utils/StringUtils.h>
#include <aws/core/utils/DateTime.h>
#include <aws/core/utils/memory/stl/AWSStringStream.h>
#include <aws/smithy/identity/signer/built-in/SigV4Signer.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include "ProducerLog.h"
#include "Common.h"
#include <Windows.h>

int AWSRecognition::Start(ConfigBase* config, IRecCallback* callback, std::string& sourceLanguage) {
    LogI("begin");

	if (config == nullptr) {
		LogE("invalid config");
		return -1;
	}
	if (callback == nullptr) {
		LogE("invalid callback");
		return -1;
	}

	callback_ = callback;
	source_language_ = sourceLanguage;
	LogI("source language : %s", source_language_.c_str());
	Config* cfg = (Config*)config;
	secretid_ = cfg->secretid;
	secretkey_ = cfg->secretkey;
	LogI("secret key id : [%s], secret key : [%s]", secretid_.c_str(), secretkey_.c_str());

	run_ = true;
	worker_thread_ = std::thread([this]() {
		Aws::Auth::AWSCredentials cred = Aws::Auth::AWSCredentials(secretid_, secretkey_);
		Aws::Client::ClientConfiguration clientConfig;
		//clientConfig.httpLibOverride = Aws::Http::TransferLibType::WIN_INET_CLIENT;
		//const Aws::String region = Aws::Region::AP_NORTHEAST_1;
		//clientConfig.region = region;
		//clientConfig.proxyHost = "127.0.0.1";
		//clientConfig.proxyPort = 7890;
		clientConfig.disableIMDS = true;
		clientConfig.disableImdsV1 = true;

		auto credsProvider = Aws::MakeShared<Aws::Auth::SimpleAWSCredentialsProvider>("transcribe", cred);

		transcribe_client_ = new (std::nothrow) Aws::TranscribeStreamingService::TranscribeStreamingServiceClient(cred, clientConfig);
		if (transcribe_client_ == nullptr) {
			LogE("create aws transcribe client failed");
			if (callback_ != nullptr) {
				callback_->OnASRStatus(this, STATUS_ERROR);
			}
			return;
		}

		transcribe_handler_.SetInitialResponseCallbackEx(
			[](const Aws::TranscribeStreamingService::Model::StartStreamTranscriptionInitialResponse&,
				const Aws::Utils::Event::InitialResponseType) {
				LogI("on initial response");
		});

		transcribe_handler_.SetOnErrorCallback(
			[](const Aws::Client::AWSError<Aws::TranscribeStreamingService::TranscribeStreamingServiceErrors>& error) {
				LogE("error message : %s", error.GetMessage().c_str());
			});

		transcribe_handler_.SetTranscriptEventCallback([this](const Aws::TranscribeStreamingService::Model::TranscriptEvent& ev) {
			for (auto&& r : ev.GetTranscript().GetResults()) {
				if (!r.GetIsPartial()) {
					for (auto&& alt : r.GetAlternatives()) {
						if (callback_ != nullptr) {
							std::string text = alt.GetTranscript();
							LogI("text : %s", text.c_str());
							callback_->OnASRResult(this, text);
						}
					}
				}
			}
			});

		transcribe_request_.SetLanguageCode(GetLanguageCode(source_language_));
		transcribe_request_.SetMediaSampleRateHertz(48000);
		transcribe_request_.SetMediaEncoding(Aws::TranscribeStreamingService::Model::MediaEncoding::pcm);
		transcribe_request_.SetEventStreamHandler(transcribe_handler_);

		transcribe_client_->StartStreamTranscriptionAsync(transcribe_request_, std::bind(&AWSRecognition::OnStreamReady, this, std::placeholders::_1),
			std::bind(&AWSRecognition::OnResponse, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
	});

	LogI("success");
	return 0;
}

void AWSRecognition::Stop(){
	LogI("begin");

	do {
		std::unique_lock<std::mutex> lock(mutex_);
		run_ = false;
		condition_.notify_one();
	} while (0);

	if (worker_thread_.joinable()) {
		worker_thread_.join();
	}

	if (transcribe_client_ != nullptr) {
		delete transcribe_client_;
		transcribe_client_ = nullptr;
	}
	LogI("success");
}

int AWSRecognition::Process(const uint8_t* audio, const size_t len, int samplerate, int channels) {

	if (!ready_.load()) {
		LogW("recognition not ready, discard audio data");
		return 0;
	}

	if (len / 2 / channels != AUDIO_10MS_SAMPLES_PER_CHANNEL) {
		LogE("invalid frame size");
		return 0;
	}

	std::unique_lock<std::mutex> lock(mutex_);
	if (current_audio_len_ == audio_data_len_) {
		memmove(&audio_data_[0], &audio_data_[AUDIO_10MS_DATA_LEN], current_audio_len_ - AUDIO_10MS_DATA_LEN);
		current_audio_len_ -= AUDIO_10MS_DATA_LEN;
	}

	memcpy(&audio_data_[current_audio_len_], audio, AUDIO_10MS_DATA_LEN);
	current_audio_len_ += AUDIO_10MS_DATA_LEN;
	if (current_audio_len_ == audio_data_len_) {
		condition_.notify_one();
	}
	return 0;
}

void AWSRecognition::OnStreamReady(Aws::TranscribeStreamingService::Model::AudioStream& stream) {
	LogI("enter");

	ready_.store(true);
	if (!stream) {
		LogE("failed to create stream");
	}

	while (run_) {
		std::unique_lock<std::mutex> lock(mutex_);
		if (run_ && current_audio_len_ != audio_data_len_) {
			condition_.wait(lock);
		}

		if (!run_) {
			LogI("worker thread finish");
			break;
		}

		if (current_audio_len_ != audio_data_len_) {
			continue;
		}

		Aws::Vector<uint8_t> data(audio_data_len_ / 2, 0);
		int j = 0;
		short* dst = (short*)(data.data());
		short* src = (short*)(&audio_data_[0]);
		for (int i = 0; i < audio_data_len_ / 2; i += 2) {
			dst[j++] = src[i];
		}

		Aws::TranscribeStreamingService::Model::AudioEvent audioEvent(std::move(data));
		if (!stream.WriteAudioEvent(audioEvent)) {
			LogE("write audio event failed");
		}
		else {
			LogI("write audio event success");
		}

		current_audio_len_ = 0;
	}

	LogI("exit");
}

void AWSRecognition::OnResponse(
	const TranscribeStreamingServiceClient*,
	const Aws::TranscribeStreamingService::Model::StartStreamTranscriptionRequest&,
	const Aws::TranscribeStreamingService::Model::StartStreamTranscriptionOutcome& outcome,
	const std::shared_ptr<const Aws::Client::AsyncCallerContext>&) {
	if (outcome.IsSuccess()) {
		LogI("success");
	}
	else {
		LogE("failed");
	}
}
