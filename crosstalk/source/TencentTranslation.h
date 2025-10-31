#pragma once

#include "ITranslation.h"
#include "tencentcloud/tmt/v20180321/TmtClient.h"

using namespace TencentCloud::Tmt::V20180321;

class TencentTranslation : public ITranslation {
public:
	struct Config : public ConfigBase {
		std::string appid;
		std::string secretid;
		std::string secretkey;
	};

	// 通过 ITranslation 继承
	virtual int Start(ConfigBase* config, ITransCallback* callback, std::string& sourceLanguage, std::string targetLanguage) override;
	virtual void Stop() override;
	virtual int Process(std::string& original) override;

private:
	void OnTranslateResult(const TmtClient*,
		const Model::TextTranslateRequest&,
		TmtClient::TextTranslateOutcome,
		const std::shared_ptr<const TencentCloud::AsyncCallerContext>&);


private:
	
	TmtClient* tmtClient_ = nullptr;
	std::string appid_ = "";
	std::string secretid_ = "";
	std::string secretkey_ = "";
	std::string source_language_ = "zh";
	std::string target_language_ = "en";
	ITransCallback* callback_ = nullptr;
};