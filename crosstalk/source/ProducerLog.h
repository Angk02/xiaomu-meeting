#pragma once

#include "dshowcapture.hpp"

void LogMessage(const char* fmt, ...);

void LogError(const char* fmt, ...);

void LogWarning(const char* fmt, ...);

void LogDebug(const char* fmt, ...);

void DshowLogCallback(DShow::LogType type, const wchar_t* msg, void* param);

#define FILENAME(x) strrchr(x,'\\')?strrchr(x,'\\')+1:x

#define LogI(fmt, ...) LogMessage("%s " fmt ", file:%s line:%d", __FUNCTION__, __VA_ARGS__, FILENAME(__FILE__), __LINE__)
#define LogE(fmt, ...) LogError("%s " fmt ", file:%s line:%d", __FUNCTION__, __VA_ARGS__, FILENAME(__FILE__), __LINE__)
#define LogW(fmt, ...) LogWarning("%s " fmt ", file:%s line:%d", __FUNCTION__, __VA_ARGS__, FILENAME(__FILE__), __LINE__)
#define LogD(fmt, ...) LogDebug("%s " fmt ", file:%s line:%d", __FUNCTION__, __VA_ARGS__, FILENAME(__FILE__), __LINE__)