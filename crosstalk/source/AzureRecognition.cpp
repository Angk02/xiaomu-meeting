#include "AzureRecognition.h"
#include "ProducerLog.h"

int AzureRecognition::Start(ConfigBase* config, IRecCallback* callback, std::string& sourceLanguage) {
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
        return -1;
    }

    Config* cfg = (Config*)config;
    secretkey_ = cfg->secretkey;
    LogI("secret key : [%s]", secretkey_.c_str());

    speech_config_ = SpeechConfig::FromSubscription(secretkey_, "eastus");
    if (speech_config_ == nullptr) {
        LogE("create speech config failed");
        swr_free(&resampleContext_);
        resampleContext_ = nullptr;
        return -1;
    }
    speech_config_->SetSpeechRecognitionLanguage(GetLanguageBCP47(source_language_));

    format_ = AudioStreamFormat::GetWaveFormatPCM(16000, 16, 1);
    if (format_ == nullptr) {
        LogE("create audio format failed");
        swr_free(&resampleContext_);
        resampleContext_ = nullptr;
        return -1;
    }
    LogI("create audio format success");

    stream_ = AudioInputStream::CreatePushStream(format_);
    if (stream_ == nullptr) {
        LogE("create audio stream failed");
        swr_free(&resampleContext_);
        resampleContext_ = nullptr;
        return -1;
    }
    LogI("create audio stream success");

    audio_config_ = AudioConfig::FromStreamInput(stream_);
    if (audio_config_ == nullptr) {
        LogE("create audio config failed");
        swr_free(&resampleContext_);
        resampleContext_ = nullptr;
        return -1;
    }
    LogI("create audio config success");

    recognizer_ = SpeechRecognizer::FromConfig(speech_config_, audio_config_);
    if (recognizer_ == nullptr) {
        LogE("create speech recognizer failed");
        swr_free(&resampleContext_);
        resampleContext_ = nullptr;
        return -1;
    }
    LogI("create speech recognizer success");

    recognizer_->Recognized.Connect([this](const SpeechRecognitionEventArgs& e) {
        if (e.Result->Reason == ResultReason::RecognizedSpeech) {
            if (callback_ != nullptr) {
                std::string text = e.Result->Text;
                callback_->OnASRResult(this, text);
            }
        }
    });

    recognizer_->Canceled.Connect([](const SpeechRecognitionCanceledEventArgs& e) {
        LogI("recognition cancled, reason : %d", e.Reason);
        if (e.Reason == CancellationReason::Error) {
            LogW("recognition cancled error, error code : %d, error message : %s", e.ErrorCode, e.ErrorDetails.c_str());
        }
    });

    recognizer_->StartContinuousRecognitionAsync().get();
    LogI("success");
    return 0;
}

void AzureRecognition::Stop() {
    LogI("begion");
    if (recognizer_ != nullptr) {
        recognizer_->StopContinuousRecognitionAsync().get();
    }

    swr_free(&resampleContext_);
    resampleContext_ = nullptr;

    LogI("success");
}

int AzureRecognition::Process(const uint8_t* audio, const size_t len, int samplerate, int channels) {
    if (len / 2 / channels != AUDIO_10MS_SAMPLES_PER_CHANNEL) {
        LogE("invalid frame size");
        return 0;
    }

    memcpy(&audio_data_[current_audio_len_], audio, AUDIO_10MS_DATA_LEN);
    current_audio_len_ += AUDIO_10MS_DATA_LEN;
    if (current_audio_len_ == audio_data_len_) {

        uint8_t outaudio[3200];
        uint8_t* dst[] = { outaudio };
        uint8_t* src[] = { (uint8_t*)audio_data_ };
        int convert_samples = swr_convert(resampleContext_, dst, 1600, src, audio_data_len_ / 2 / AUDIO_CHANNELS);

        if (stream_ != nullptr) {
            stream_->Write(outaudio, convert_samples*2);
        }
        current_audio_len_ = 0;
    }
	return 0;
}
