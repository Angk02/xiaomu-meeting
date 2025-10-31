#pragma once

#include "Producer.h"
#include "AudioCapture.h"
#include "IRecognition.h"
#include "ITranslation.h"
#include "ITextToSpeech.h"
#include "WASAPIRenderer.h"
#include "IRecAndTrans.h"
#include <aws/core/Aws.h>

#define DEFAULT_ASR 0
#define AMAZON_ASR 0
#define AZURE_ASR 1

class ProducerImpl : public IProducer,
					 public AudioCaptrue::IAudioSink,
					 public IRecognition::IRecCallback,
					 public ITranslation::ITransCallback,
					 public ITextToSpeech::ITTSCallback {
public:
	~ProducerImpl();
	virtual int Start(std::string& captureName, std::string& playoutName, ASRAndTransType type, ICallback* callback,
					  std::string sourceLanguage = "zh", std::string targetLanguage = "en") override;
	virtual int Stop() override;
	virtual void OnAudioFrame(const uint8_t* audio, const size_t len, int samplerate, int channels) override;
	virtual void OnASRStatus(IRecognition* obj, Status status) override;
	virtual void OnASRResult(IRecognition* obj, std::string& result) override;
	virtual void OnTransStatus(ITranslation* obj, Status status) override;
	virtual void OnTransResult(ITranslation* obj, std::string& original, std::string& translation) override;
	virtual void OnTTSStatus(ITextToSpeech* obj, Status status) override;
	virtual void OnTTSResult(ITextToSpeech* obj, const uint8_t* audio, const size_t len, int samplerate, int channels) override;

private:
	AudioCaptrue* capture_ = nullptr;
#if AZURE_ASR
	IRecAndTrans* rec_trans_ = nullptr;
#else
	IRecognition* asr_ = nullptr;
	ITranslation* trans_ = nullptr;
#endif
	ITextToSpeech* tts_ = nullptr;
	CWASAPIRenderer* render_ = nullptr;
	LoopbackBuffer* buffer_ = nullptr;
	ICallback* callback_ = nullptr;
	//Aws::SDKOptions options_;
};