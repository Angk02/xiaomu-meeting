#include "AzureRecAndTrans.h"
#include "ProducerLog.h"

int AzureRecAndTrans::Start(ITranslation::ConfigBase* config, ITransCallback* callback, std::string& sourceLanguage, std::string targetLanguage) {
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
	target_language_ = targetLanguage;
	LogI("source language : %s, target language : %s", source_language_.c_str(), target_language_.c_str());
	Config* cfg = (Config*)config;
	secretkey_ = cfg->secretkey;
    region_ = cfg->region;
    LogI("region : %s", region_.c_str());

    run_ = true;
    work_thread_ = std::thread(std::bind(&AzureRecAndTrans::worker, this));

    LogI("success");
	return 0;
}

void AzureRecAndTrans::Stop() {
    LogI("begin");
    do {
        std::unique_lock<std::mutex> lock(mutex_);
        run_ = false;
        condition_.notify_one();
    } while (0);
    if (work_thread_.joinable()) {
        LogI("wait for work thread exit");
        work_thread_.join();
        LogI("work thread exit success");
    }
    LogI("success");
}

void AzureRecAndTrans::worker() {
    LogI("enter");
    AVChannelLayout out_ch_layout;
    av_channel_layout_default(&out_ch_layout, 1);

    AVChannelLayout in_ch_layout;
    av_channel_layout_default(&in_ch_layout, AUDIO_CHANNELS);
    int ret = swr_alloc_set_opts2(&resampleContext_, &out_ch_layout, AV_SAMPLE_FMT_S16, 16000, &in_ch_layout, AV_SAMPLE_FMT_S16, AUDIO_SAMPLERATE, 0, nullptr);
    if (ret < 0) {
        LogE("malloc resample context failed, error message : %s", GetFFmpegErrorString(ret).c_str());
        resampleContext_ = nullptr;
    }
    else {
        ret = swr_init(resampleContext_);
        if (ret < 0) {
            LogE("resample init failed, error message : %s", GetFFmpegErrorString(ret).c_str());
            swr_free(&resampleContext_);
            resampleContext_ = nullptr;
        }
    }

    if (resampleContext_ == nullptr) {
        LogE("create resample context failed");
        if (callback_ != nullptr) {
            callback_->OnTransStatus(this, STATUS_ERROR);
        }
        return;
    }

    speech_config_ = SpeechTranslationConfig::FromSubscription(secretkey_, region_);
    if (speech_config_ == nullptr) {
        LogE("create speech config failed");
        if (callback_ != nullptr) {
            callback_->OnTransStatus(this, STATUS_ERROR);
        }
        swr_free(&resampleContext_);
        resampleContext_ = nullptr;
        return;
    }
    LogI("create speech config success");
    speech_config_->SetSpeechRecognitionLanguage(GetLanguageBCP47(source_language_));
    speech_config_->AddTargetLanguage(GetLanguageBCP47(target_language_));

    format_ = AudioStreamFormat::GetWaveFormatPCM(16000, 16, 1);
    if (format_ == nullptr) {
        LogE("create audio format failed");
        if (callback_ != nullptr) {
            callback_->OnTransStatus(this, STATUS_ERROR);
        }
        swr_free(&resampleContext_);
        resampleContext_ = nullptr;
        return;
    }
    LogI("create audio format success");

    stream_ = AudioInputStream::CreatePushStream(format_);
    if (stream_ == nullptr) {
        LogE("create audio stream failed");
        if (callback_ != nullptr) {
            callback_->OnTransStatus(this, STATUS_ERROR);
        }
        swr_free(&resampleContext_);
        resampleContext_ = nullptr;
        return;
    }
    LogI("create audio stream success");

    audio_config_ = AudioConfig::FromStreamInput(stream_);
    if (audio_config_ == nullptr) {
        LogE("create audio config failed");
        if (callback_ != nullptr) {
            callback_->OnTransStatus(this, STATUS_ERROR);
        }
        swr_free(&resampleContext_);
        resampleContext_ = nullptr;
        return;
    }
    LogI("create audio config success");

    recognizer_ = TranslationRecognizer::FromConfig(speech_config_, audio_config_);
    if (recognizer_ == nullptr) {
        LogE("create translation recognizer failed");
        if (callback_ != nullptr) {
            callback_->OnTransStatus(this, STATUS_ERROR);
        }
        swr_free(&resampleContext_);
        resampleContext_ = nullptr;
        return;
    }

    recognizer_->Recognized.Connect([this](const TranslationRecognitionEventArgs& e) {
        if (e.Result->Reason == ResultReason::TranslatedSpeech) {
            std::string origin = e.Result->Text;
            std::string trans = e.Result->Translations.begin()->second;
            if (callback_ != nullptr) {
                callback_->OnTransResult(this, origin, trans);
            }
        }
        });

    recognizer_->Canceled.Connect([](const TranslationRecognitionCanceledEventArgs& e) {
        LogI("translation recognition cancled, reason : %d", e.Reason);
        if (e.Reason == CancellationReason::Error) {
            LogW("recognition cancled error, error code : %d, error message : %s", e.ErrorCode, e.ErrorDetails.c_str());
        }
        });

    recognizer_->StartContinuousRecognitionAsync().get();
    if (callback_ != nullptr) {
        callback_->OnTransStatus(this, STATUS_STARTED);
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

        uint8_t outaudio[3200];
        uint8_t* dst[] = { outaudio };
        uint8_t* src[] = { (uint8_t*)audio_data_ };
        int convert_samples = swr_convert(resampleContext_, dst, 1600, src, audio_data_len_ / 2 / AUDIO_CHANNELS);

        if (stream_ != nullptr) {
            stream_->Write(outaudio, convert_samples * 2);
        }
        current_audio_len_ = 0;
    }

    //if (recognizer_ != nullptr) {
    //    recognizer_->StopContinuousRecognitionAsync().get();
    //}

    if (resampleContext_ != nullptr) {
        swr_free(&resampleContext_);
        resampleContext_ = nullptr;
    }
    LogI("exit");
}

int AzureRecAndTrans::Process(const uint8_t* audio, const size_t len, int samplerate, int channels) {
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
