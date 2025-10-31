// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved
//
#pragma once
#include "ProducerLog.h"
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include <AudioPolicy.h>
#include <mutex>
#include <vector>

#define LOOPBACK_BUFFER_SIZE (1024 * 1024 * 10) 

class LoopbackBuffer
{
public:
    LoopbackBuffer()
        : m_Buffer(nullptr),
        m_Size(0),
        m_WriteOffset(0),
        m_ReadOffset(0)
    {
    }

    ~LoopbackBuffer()
    {
        if (m_Buffer)
        {
            delete[] m_Buffer;
            m_Buffer = nullptr;
        }
    }

    bool Initialize(ULONG bufferSize = LOOPBACK_BUFFER_SIZE)
    {
        m_Buffer = new (std::nothrow) unsigned char[bufferSize];
        if (!m_Buffer)
        {
            return false;
        }

        m_Size = bufferSize;
        m_WriteOffset = 0;
        m_ReadOffset = 0;
        return true;
    }

    void Write(const PUCHAR data, ULONG length)
    {
        if (!m_Buffer || length > m_Size) {
            LogE("invalid buffer or length");
            return;
        }

        std::unique_lock<std::mutex> lock(m_mutex);
        ULONG space = m_Size - m_WriteOffset;
        if (space >= length)
        {
            RtlCopyMemory(m_Buffer + m_WriteOffset, data, length);
        }
        else
        {
            RtlCopyMemory(m_Buffer + m_WriteOffset, data, space);
            RtlCopyMemory(m_Buffer, data + space, length - space);
        }

        m_WriteOffset = (m_WriteOffset + length) % m_Size;
    }

    ULONG Read(PUCHAR outBuffer, ULONG length)
    {
        if (!m_Buffer)
            return 0;

        std::unique_lock<std::mutex> lock(m_mutex);

        ULONG available = (m_WriteOffset >= m_ReadOffset)
            ? (m_WriteOffset - m_ReadOffset)
            : (m_Size - m_ReadOffset + m_WriteOffset);

        ULONG toRead = std::min(length, available);
        ULONG tailSpace = m_Size - m_ReadOffset;

        if (tailSpace >= toRead)
        {
            RtlCopyMemory(outBuffer, m_Buffer + m_ReadOffset, toRead);
        }
        else
        {
            RtlCopyMemory(outBuffer, m_Buffer + m_ReadOffset, tailSpace);
            RtlCopyMemory(outBuffer + tailSpace, m_Buffer, toRead - tailSpace);
        }

        m_ReadOffset = (m_ReadOffset + toRead) % m_Size;

        return toRead;
    }

    ULONG AvailableBytes() {
        if (!m_Buffer)
            return 0;

        std::unique_lock<std::mutex> lock(m_mutex);
        ULONG available = (m_WriteOffset >= m_ReadOffset)
            ? (m_WriteOffset - m_ReadOffset)
            : (m_Size - m_ReadOffset + m_WriteOffset);
        return available;
    }

private:
    PUCHAR m_Buffer;
    ULONG  m_Size;
    ULONG  m_WriteOffset;
    ULONG  m_ReadOffset;
    std::mutex m_mutex;
};

class CWASAPIRenderer : public IUnknown
{
public:
    //  Public interface to CWASAPIRenderer.
    enum RenderSampleType
    {
        SampleTypeFloat,
        SampleType16BitPCM,
    };

    CWASAPIRenderer(std::string& deviceName);
    ~CWASAPIRenderer(void);
    bool Initialize(UINT32 EngineLatency);
    void Shutdown();
    bool Start(LoopbackBuffer *RenderBufferQueue);
    void Stop();
    WORD ChannelCount() { return _MixFormat.nChannels; }
    UINT32 SamplesPerSecond() { return _MixFormat.nSamplesPerSec; }
    UINT32 BytesPerSample() { return _MixFormat.wBitsPerSample / 8; }
    RenderSampleType SampleType() { return _RenderSampleType; }
    UINT32 FrameSize() { return _FrameSize; }
    UINT32 BufferSize() { return _BufferSize; }
    UINT32 BufferSizePerPeriod();
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();
    static std::vector<std::string> RenderDevices();

private:
    IMMDevice* GetRenderDevice();

    LONG    _RefCount;
    //
    //  Core Audio Rendering member variables.
    //
    IMMDevice * _Endpoint;
    IAudioClient *_AudioClient;
    IAudioRenderClient *_RenderClient;

    HANDLE      _RenderThread;
    HANDLE      _ShutdownEvent;
    WAVEFORMATEX _MixFormat;
    UINT32      _FrameSize;
    UINT32      _playBlockSize;
    RenderSampleType _RenderSampleType;
    UINT32      _BufferSize;
    LONG        _EngineLatencyInMS;
    std::string _DeviceName;

    //
    //  Render buffer management.
    //
    LoopbackBuffer* _RenderBufferQueue;

    static DWORD __stdcall WASAPIRenderThread(LPVOID Context);
    DWORD DoRenderThread();

    //
    //  IUnknown
    //
    STDMETHOD(QueryInterface)(REFIID iid, void **pvObject);

    //
    //  Utility functions.
    //
    bool CalculateMixFormatType();
    bool InitializeAudioEngine();
    bool LoadFormat();
};
