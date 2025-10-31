#include "TencentTranslation.h"
#include "ProducerLog.h"

int TencentTranslation::Start(ConfigBase* config, ITransCallback* callback, std::string& sourceLanguage, std::string targetLanguage) {
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
	appid_ = cfg->appid;
	secretid_ = cfg->secretid;
	secretkey_ = cfg->secretkey;

	TencentCloud::Credential cred = TencentCloud::Credential(secretid_, secretkey_);
	TencentCloud::HttpProfile httpProfile = TencentCloud::HttpProfile();
	httpProfile.SetKeepAlive(true);  // 状态保持，默认是False
	httpProfile.SetEndpoint("tmt.tencentcloudapi.com");  // 指定接入地域域名(默认就近接入) 
	httpProfile.SetReqTimeout(30);  // 请求超时时间，单位为秒(默认60秒)
	httpProfile.SetConnectTimeout(30); // 响应超时时间，单位是秒(默认是60秒)
	TencentCloud::ClientProfile clientProfile = TencentCloud::ClientProfile(httpProfile);

	tmtClient_ = new(std::nothrow) TmtClient(cred, "ap-beijing", httpProfile);
	if (tmtClient_ == nullptr) {
		LogE("create tmt client failed");
		return -1;
	}

	LogI("success");
	return 0;
}

void TencentTranslation::Stop() {
	LogI("begin");
	if (tmtClient_ != nullptr) {
		delete tmtClient_;
		tmtClient_ = nullptr;
	}
	LogI("success");
}

int TencentTranslation::Process(std::string& original) {
	//LogI("text : %s", original.c_str());
	Model::TextTranslateRequest request;
	request.SetSource(source_language_);
	request.SetSourceText(original);
	request.SetTarget(target_language_);
	request.SetProjectId(0);

	tmtClient_->TextTranslateAsync(request,
		std::bind(&TencentTranslation::OnTranslateResult, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));

	return 0;
}

void TencentTranslation::OnTranslateResult(const TmtClient* client, const Model::TextTranslateRequest& request,
	TmtClient::TextTranslateOutcome outcome,
	const std::shared_ptr<const TencentCloud::AsyncCallerContext>& context) {
	if (outcome.IsSuccess()) {
		auto result = outcome.GetResult();
		if (callback_ != nullptr) {
			std::string originalText = request.GetSourceText();
			std::string translateText = result.GetTargetText();
			callback_->OnTransResult(this, originalText, translateText);
		}
	}
	else {
		LogE("translation failed");
	}
}
