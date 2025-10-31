// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved
//

#include <windows.h>
#include <strsafe.h>
#include <objbase.h>
#pragma warning(push)
#pragma warning(disable : 4201)
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#pragma warning(pop)
#include <assert.h>
#include <avrt.h>
#include "WASAPIRenderer.h"
#include "ProducerLog.h"
#include <mmsystem.h>
#include <functiondiscoverykeys_devpkey.h>
#include "Common.h"

//
//  A simple WASAPI Render client.
//

template <class T> void SafeRelease(T** ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

CWASAPIRenderer::CWASAPIRenderer(std::string& deviceName) :
    _RefCount(1),
    _Endpoint(NULL),
    _AudioClient(NULL),
    _RenderClient(NULL),
    _RenderThread(NULL),
    _ShutdownEvent(NULL),
    //_MixFormat(NULL),
    _RenderBufferQueue(0)
{
    if(_Endpoint != nullptr)
        _Endpoint->AddRef();    // Since we're holding a copy of the endpoint, take a reference to it.  It'll be released in Shutdown();
    _FrameSize = 0;
    _playBlockSize = 0;
    _BufferSize = 0;
    _EngineLatencyInMS = 10;
    _RenderSampleType = SampleType16BitPCM;
    _DeviceName = deviceName;
}

//
//  Empty destructor - everything should be released in the Shutdown() call.
//
CWASAPIRenderer::~CWASAPIRenderer(void) 
{
}
#define PERIODS_PER_BUFFER 4
//
//  Initialize WASAPI in event driven mode, associate the audio client with our samples ready event handle, and retrieve 
//  a render client for the transport.
//
bool CWASAPIRenderer::InitializeAudioEngine()
{
    REFERENCE_TIME bufferDuration = _EngineLatencyInMS*10000*PERIODS_PER_BUFFER;
    REFERENCE_TIME periodicity = _EngineLatencyInMS*10000;

    //
    //  We initialize the engine with a periodicity of _EngineLatencyInMS and a buffer size of PERIODS_PER_BUFFER times the latency - this ensures 
    //  that we will always have space available for rendering audio.  We only need to do this for exclusive mode timer driven rendering.
    //
    HRESULT hr = _AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_NOPERSIST, 
        bufferDuration, 
        periodicity,
        &_MixFormat, 
        NULL);
    if (FAILED(hr))
    {
        LogE("Unable to initialize audio client: %x.", hr);
        return false;
    }

    //
    //  Retrieve the buffer size for the audio client.
    //
    hr = _AudioClient->GetBufferSize(&_BufferSize);
    if(FAILED(hr))
    {
        LogE("Unable to get audio client buffer: %x. ", hr);
        return false;
    }

    hr = _AudioClient->GetService(IID_PPV_ARGS(&_RenderClient));
    if (FAILED(hr))
    {
        LogE("Unable to get new render client: %x.", hr);
        return false;
    }

    return true;
}
//
//  That buffer duration is calculated as being PERIODS_PER_BUFFER x the
//  periodicity, so each period we're going to see 1/PERIODS_PER_BUFFERth 
//  the size of the buffer.
//
UINT32 CWASAPIRenderer::BufferSizePerPeriod()
{
    return _BufferSize / PERIODS_PER_BUFFER;
}

//
//  Retrieve the format we'll use to rendersamples.
//
//  Start with the mix format and see if the endpoint can render that.  If not, try
//  the mix format converted to an integer form (most audio solutions don't support floating 
//  point rendering and the mix format is usually a floating point format).
//
bool CWASAPIRenderer::LoadFormat()
{
    WAVEFORMATEX* pWfxOut = NULL;
    WAVEFORMATEX Wfx = WAVEFORMATEX();
    WAVEFORMATEX* pWfxClosestMatch = NULL;
    HRESULT hr = _AudioClient->GetMixFormat(&pWfxOut);
    if (FAILED(hr))
    {
        LogE("Unable to get mix format on audio client: %x.", hr);
        return false;
    }
    assert(pWfxOut != NULL);
    LogI("Audio Engine's current rendering mix format, samplerate : %d, channels : %d, bits per sample : %d,"
        "block align : %d, average bytes per second : %d", pWfxOut->nSamplesPerSec,
        pWfxOut->nChannels, pWfxOut->wBitsPerSample, pWfxOut->nBlockAlign, pWfxOut->nAvgBytesPerSec);
    CoTaskMemFree(pWfxOut);
    //_MixFormat->wFormatTag = WAVE_FORMAT_PCM;
    //_MixFormat->nSamplesPerSec = 48000;
    //_MixFormat->nChannels = 2;
    //_MixFormat->wBitsPerSample = 16;
    //_MixFormat->nBlockAlign = _MixFormat->nChannels * _MixFormat->wBitsPerSample / 8;
    //_MixFormat->nAvgBytesPerSec = _MixFormat->nSamplesPerSec * _MixFormat->nBlockAlign;
    //_MixFormat->cbSize = 0;

    Wfx.wFormatTag = WAVE_FORMAT_PCM;
    Wfx.wBitsPerSample = 16;
    Wfx.cbSize = 0;
    Wfx.nChannels = 2;
    Wfx.nSamplesPerSec = 48000;
    Wfx.nBlockAlign = Wfx.nChannels * Wfx.wBitsPerSample / 8;
    Wfx.nAvgBytesPerSec = Wfx.nSamplesPerSec * Wfx.nBlockAlign;

    hr = _AudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,&Wfx, &pWfxClosestMatch);
    if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT)
    {
        LogE("Device does not natively support the mix format, converting to PCM.");
        LogI("Audio Engine's closest rendering mix format, samplerate : %d, channels : %d, bits per sample : %d,"
            "block align : %d, average bytes per second : %d", pWfxClosestMatch->nSamplesPerSec,
            pWfxClosestMatch->nChannels, pWfxClosestMatch->wBitsPerSample, pWfxClosestMatch->nBlockAlign, pWfxClosestMatch->nAvgBytesPerSec);
        CoTaskMemFree(pWfxClosestMatch);
        return false;
        //
        //  If the mix format is a float format, just try to convert the format to PCM.
        //
        /*if (_MixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        {
            _MixFormat->wFormatTag = WAVE_FORMAT_PCM;
            _MixFormat->wBitsPerSample = 16;
            _MixFormat->nBlockAlign = (_MixFormat->wBitsPerSample / 8) * _MixFormat->nChannels;
            _MixFormat->nAvgBytesPerSec = _MixFormat->nSamplesPerSec*_MixFormat->nBlockAlign;
        }
        else if (_MixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && 
            reinterpret_cast<WAVEFORMATEXTENSIBLE *>(_MixFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
        {
            WAVEFORMATEXTENSIBLE *waveFormatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(_MixFormat);
            waveFormatExtensible->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            waveFormatExtensible->Format.wBitsPerSample = 16;
            waveFormatExtensible->Format.nBlockAlign = (_MixFormat->wBitsPerSample / 8) * _MixFormat->nChannels;
            waveFormatExtensible->Format.nAvgBytesPerSec = waveFormatExtensible->Format.nSamplesPerSec*waveFormatExtensible->Format.nBlockAlign;
            waveFormatExtensible->Samples.wValidBitsPerSample = 16;
        }
        else
        {
            LogE("Mix format is not a floating point format.");
            return false;
        }

        hr = _AudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,_MixFormat,NULL);
        if (FAILED(hr))
        {
            LogE("Format is not supported");
            return false;
        }*/
    }
    CoTaskMemFree(pWfxClosestMatch);

    RtlCopyMemory(&_MixFormat, &Wfx, sizeof(Wfx));
    LogI("playout samplerate : %d, channels : %d, bits per sample : %d", _MixFormat.nSamplesPerSec, _MixFormat.nChannels, _MixFormat.wBitsPerSample);
    _FrameSize = _MixFormat.nBlockAlign;
    _playBlockSize = _MixFormat.nSamplesPerSec / 100;
    if (!CalculateMixFormatType())
    {
        return false;
    }
    return true;
}

//
//  Crack open the mix format and determine what kind of samples are being rendered.
//
bool CWASAPIRenderer::CalculateMixFormatType()
{
    if (_MixFormat.wFormatTag == WAVE_FORMAT_PCM || 
        _MixFormat.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            reinterpret_cast<WAVEFORMATEXTENSIBLE *>(&_MixFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
    {
        if (_MixFormat.wBitsPerSample == 16)
        {
            _RenderSampleType = SampleType16BitPCM;
        }
        else
        {
            LogE("Unknown PCM integer sample type");
            return false;
        }
    }
    else if (_MixFormat.wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
             (_MixFormat.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
               reinterpret_cast<WAVEFORMATEXTENSIBLE *>(&_MixFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
    {
        _RenderSampleType = SampleTypeFloat;
    }
    else 
    {
        LogE("unrecognized device format.");
        return false;
    }
    return true;
}

static int32_t GetDeviceName(IMMDevice* pDevice,
    LPWSTR pszBuffer,
    int bufferLen) {

    static const WCHAR szDefault[] = L"<Device not available>";

    HRESULT hr = E_FAIL;
    IPropertyStore* pProps = NULL;
    PROPVARIANT varName;

    if (pDevice != NULL) {
        hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
        if (FAILED(hr)) {
            LogE("IMMDevice::OpenPropertyStore failed, hr = 0x%X", hr);
        }
    }

    // Initialize container for property value.
    PropVariantInit(&varName);

    if (SUCCEEDED(hr)) {
        // Get the endpoint device's friendly-name property.
        hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
        if (FAILED(hr)) {
            LogE("IPropertyStore::GetValue failed, hr = 0x%X", hr);
        }
    }

    if ((SUCCEEDED(hr)) && (VT_EMPTY == varName.vt)) {
        hr = E_FAIL;
        LogE("IPropertyStore::GetValue returned no value, hr = 0x%X", hr);
    }

    if ((SUCCEEDED(hr)) && (VT_LPWSTR != varName.vt)) {
        // The returned value is not a wide null terminated string.
        hr = E_UNEXPECTED;
        LogE("IPropertyStore::GetValue returned unexpected type, hr = 0x%X", hr);
    }

    int ret = 0;
    if (SUCCEEDED(hr) && (varName.pwszVal != NULL)) {
        // Copy the valid device name to the provided ouput buffer.
        wcsncpy_s(pszBuffer, bufferLen, varName.pwszVal, _TRUNCATE);
    }
    else {
        // Failed to find the device name.
        wcsncpy_s(pszBuffer, bufferLen, szDefault, _TRUNCATE);
        return -1;
    }

    PropVariantClear(&varName);
    SafeRelease(&pProps);

    return ret;
}

//static
std::vector<std::string> CWASAPIRenderer::RenderDevices() {
    std::vector<std::string> ret;
    HRESULT hr = S_OK;
    IMMDeviceEnumerator* _ptrEnumerator = nullptr;
    IMMDeviceCollection* pCollection = nullptr;

    do {
        CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&_ptrEnumerator));
        if (_ptrEnumerator == nullptr) {
            LogE("create MMDeviceEnumerator failed");
            break;
        }

        hr = _ptrEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE,
            &pCollection);
        if (FAILED(hr)) {
            LogE("EnumAudioEndpoints failed");
            break;
        }

        UINT count = 0;
        hr = pCollection->GetCount(&count);
        if (FAILED(hr)) {
            LogE("get device count failed, error code : 0x%X", hr);
            break;
        }

        for (UINT i = 0; i < count; i++) {
            IMMDevice* cur = nullptr;
            hr = pCollection->Item(i, &cur);
            if (FAILED(hr)) {
                LogW("get device at index %d failed, error code : 0x%X", i, hr);
                continue;
            }

            wchar_t name[1024];
            int32_t err = GetDeviceName(cur, name, 1024);
            if (err != 0) {
                cur->Release();
                continue;
            }

            std::string friendlyname = WideToMulti(name);
            //LogI("device index %d, name : %s", i, friendlyname.c_str());
            ret.push_back(friendlyname);
            /*if (strstr(friendlyname.c_str(), _DeviceName.c_str()) != nullptr) {
                ret = cur;
                LogI("find device with name : %s", friendlyname.c_str());
                break;
            }*/
        }
    } while (0);

    SafeRelease(&_ptrEnumerator);
    SafeRelease(&pCollection);
    return ret;
}

//High Definition Audio Device
//VirtualMic
//CABLE Input (VB-Audio Virtual Cable)
static const std::string kRenderName = std::string("CABLE Input (VB-Audio Virtual Cable)");

IMMDevice* CWASAPIRenderer::GetRenderDevice() {
    HRESULT hr = S_OK;
    IMMDevice* ret = nullptr;
    IMMDeviceEnumerator* _ptrEnumerator = nullptr;
    IMMDeviceCollection* pCollection = nullptr;

    do {
        CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&_ptrEnumerator));
        if (_ptrEnumerator == nullptr) {
            LogE("create MMDeviceEnumerator failed");
            break;
        }

        hr = _ptrEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE,
            &pCollection);
        if (FAILED(hr)) {
            LogE("EnumAudioEndpoints failed");
            break;
        }

        UINT count = 0;
        hr = pCollection->GetCount(&count);
        if (FAILED(hr)) {
            LogE("get device count failed, error code : 0x%X", hr);
            break;
        }

        for (UINT i = 0; i < count; i++) {
            IMMDevice* cur = nullptr;
            hr = pCollection->Item(i, &cur);
            if (FAILED(hr)) {
                LogW("get device at index %d failed, error code : 0x%X", i, hr);
                continue;
            }

            wchar_t name[1024];
            int32_t err = GetDeviceName(cur, name, 1024);
            if (err != 0) {
                cur->Release();
                continue;
            }

            std::string friendlyname = WideToMulti(name);
            LogI("device index %d, name : %s", i, friendlyname.c_str());
            if (strstr(friendlyname.c_str(), _DeviceName.c_str()) != nullptr) {
                ret = cur;
                LogI("find device with name : %s", friendlyname.c_str());
                break;
            }
        }
    } while (0);

    SafeRelease(&_ptrEnumerator);
    SafeRelease(&pCollection);
    return ret;
}

//
//  Initialize the renderer.
//
bool CWASAPIRenderer::Initialize(UINT32 EngineLatency)
{
    HRESULT hr = S_OK;
    if (_Endpoint == nullptr) {
        _Endpoint = GetRenderDevice();
        if (_Endpoint == nullptr) {
            LogE("GetRenderDevice failed");
            return false;
        }
    }

    //
    //  Create our shutdown and samples ready events- we want auto reset events that start in the not-signaled state.
    //
    _ShutdownEvent = CreateEventEx(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (_ShutdownEvent == NULL)
    {
        LogE("Unable to create shutdown event: %d.", GetLastError());
        return false;
    }


    //
    //  Now activate an IAudioClient object on our preferred endpoint and retrieve the mix format for that endpoint.
    //
    hr = _Endpoint->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, NULL, reinterpret_cast<void **>(&_AudioClient));
    if (FAILED(hr))
    {
        LogE("Unable to activate audio client: %x.", hr);
        return false;
    }

    //
    // Load the MixFormat.  This may differ depending on the shared mode used
    //
    if (!LoadFormat())
    {
        LogE("Failed to load the mix format");
        return false;
    }

    //
    //  Remember our configured latency in case we'll need it for a stream switch later.
    //
    _EngineLatencyInMS = EngineLatency;

    if (!InitializeAudioEngine())
    {
        return false;
    }

    return true;
}

//
//  Shut down the render code and free all the resources.
//
void CWASAPIRenderer::Shutdown()
{
    if (_RenderThread)
    {
        SetEvent(_ShutdownEvent);
        WaitForSingleObject(_RenderThread, INFINITE);
        CloseHandle(_RenderThread);
        _RenderThread = NULL;
    }

    if (_ShutdownEvent)
    {
        CloseHandle(_ShutdownEvent);
        _ShutdownEvent = NULL;
    }

    SafeRelease(&_Endpoint);
    SafeRelease(&_AudioClient);
    SafeRelease(&_RenderClient);

    //if (_MixFormat)
    //{
    //    CoTaskMemFree(_MixFormat);
    //    _MixFormat = NULL;
    //}
}


//
//  Start rendering - Create the render thread and start rendering the buffer.
//
bool CWASAPIRenderer::Start(LoopbackBuffer*RenderBufferQueue)
{
    HRESULT hr;

    BYTE* pData;
    hr = _RenderClient->GetBuffer(_BufferSize, &pData);
    if (FAILED(hr))
    {
        LogE("Failed to get buffer: %x.", hr);
        return false;
    }
    hr = _RenderClient->ReleaseBuffer(_BufferSize, AUDCLNT_BUFFERFLAGS_SILENT);

    if (FAILED(hr))
    {
        LogE("Failed to release buffer: %x.", hr);
        return false;
    }

    _RenderBufferQueue = RenderBufferQueue;
    //
    //  Now create the thread which is going to drive the renderer.
    //
    _RenderThread = CreateThread(NULL, 0, WASAPIRenderThread, this, 0, NULL);
    if (_RenderThread == NULL)
    {
        LogE("Unable to create transport thread: %x.", GetLastError());
        return false;
    }

    //
    //  We're ready to go, start rendering!
    //
    hr = _AudioClient->Start();
    if (FAILED(hr))
    {
        LogE("Unable to start render client: %x.", hr);
        return false;
    }

    return true;
}

//
//  Stop the renderer.
//
void CWASAPIRenderer::Stop()
{
    HRESULT hr;

    //
    //  Tell the render thread to shut down, wait for the thread to complete then clean up all the stuff we 
    //  allocated in Start().
    //
    if (_ShutdownEvent)
    {
        SetEvent(_ShutdownEvent);
    }

    hr = _AudioClient->Stop();
    if (FAILED(hr))
    {
        LogE("Unable to stop audio client: %x", hr);
    }

    if (_RenderThread)
    {
        WaitForSingleObject(_RenderThread, INFINITE);

        CloseHandle(_RenderThread);
        _RenderThread = NULL;
    }
}

//
//  Render thread - processes samples from the audio engine
//
DWORD CWASAPIRenderer::WASAPIRenderThread(LPVOID Context)
{
    CWASAPIRenderer *renderer = static_cast<CWASAPIRenderer *>(Context);
    return renderer->DoRenderThread();
}

DWORD CWASAPIRenderer::DoRenderThread()
{
    bool stillPlaying = true;
    HANDLE waitArray[1] = {_ShutdownEvent};
    HANDLE mmcssHandle = NULL;
    DWORD mmcssTaskIndex = 0;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        LogE("Unable to initialize COM in render thread: %x", hr);
        return hr;
    }

    //
    //  We want to make sure that our timer resolution is a multiple of the latency, otherwise the system timer cadence will
    //  cause us to starve the renderer.
    //
    //  Set the system timer to 1ms as a worst case value.
    //
    timeBeginPeriod(1);

    mmcssHandle = AvSetMmThreadCharacteristics(L"Audio", &mmcssTaskIndex);
    if (mmcssHandle == NULL)
    {
        LogE("Unable to enable MMCSS on render thread: %d", GetLastError());
    }

    while (stillPlaying)
    {
        HRESULT hr;
        //
        //  When running in timer mode, wait for half the configured latency.
        //
        DWORD waitResult = WaitForMultipleObjects(1, waitArray, FALSE, _EngineLatencyInMS/2);
        switch (waitResult)
        {
        case WAIT_OBJECT_0 + 0:     // _ShutdownEvent
            stillPlaying = false;       // We're done, exit the loop.
            break;
        case WAIT_TIMEOUT:          // Timeout
            //
            //  We need to provide the next buffer of samples to the audio renderer.  If we're done with our samples, we're done.
            //
            if (_RenderBufferQueue == NULL)
            {
                stillPlaying = false;
            }
            else
            {
                BYTE *pData;
                UINT32 padding;
                UINT32 framesAvailable;

                //
                //  We want to find out how much of the buffer *isn't* available (is padding).
                //
                hr = _AudioClient->GetCurrentPadding(&padding);
                if (SUCCEEDED(hr))
                {
                    //
                    //  Calculate the number of frames available.  We'll render
                    //  that many frames or the number of frames left in the buffer, whichever is smaller.
                    //
                    framesAvailable = _BufferSize - padding;
                    if (framesAvailable < _playBlockSize) {
                        //LogI("wait for available reach block size");
                        continue;
                    }
                    
                    int count = framesAvailable / _playBlockSize;
                    //LogI("frames available : %d, play block size : %d, count : %d", framesAvailable, _playBlockSize, count);
                    for (int i = 0; i < count; i++) {
                        hr = _RenderClient->GetBuffer(_playBlockSize, &pData);
                        if (SUCCEEDED(hr))
                        {
                            int needBytes = _playBlockSize * _FrameSize;
                            int readBytes = _RenderBufferQueue->Read(pData, needBytes);
                            if (readBytes < needBytes) {
                                RtlZeroMemory(pData + readBytes, needBytes - readBytes);
                            }

                            /*static FILE* fp = nullptr;
                            if (fp == nullptr) {
                                char pcmpath[MAX_PATH];
                                GetModuleFileNameA(nullptr, pcmpath, MAX_PATH);
                                strrchr(pcmpath, '\\')[0] = '\0';
                                strcat(pcmpath, "\\write.pcm");
                                fp = fopen(pcmpath, "wb");
                            }
                            if (fp != nullptr) {
                                fwrite(pData, needBytes, 1, fp);
                                fflush(fp);
                            }*/

                            hr = _RenderClient->ReleaseBuffer(_playBlockSize, 0);
                            if (!SUCCEEDED(hr))
                            {
                                LogE("Unable to release buffer: %x", hr);
                                stillPlaying = false;
                            }
                        }
                        else
                        {
                            LogE("Unable to release buffer: %x", hr);
                            stillPlaying = false;
                        }
                    }
                }
            }
            break;
        }
    }

    //
    //  Unhook from MMCSS.
    //
    AvRevertMmThreadCharacteristics(mmcssHandle);

    //
    //  Revert the system timer to the previous value.
    //
    timeEndPeriod(1);

    CoUninitialize();
    return 0;
}



//
//  IUnknown
//
HRESULT CWASAPIRenderer::QueryInterface(REFIID Iid, void **Object)
{
    if (Object == NULL)
    {
        return E_POINTER;
    }
    *Object = NULL;

    if (Iid == IID_IUnknown)
    {
        *Object = static_cast<IUnknown *>(this);
        AddRef();
    }
    else
    {
        return E_NOINTERFACE;
    }
    return S_OK;
}
ULONG CWASAPIRenderer::AddRef()
{
    return InterlockedIncrement(&_RefCount);
}
ULONG CWASAPIRenderer::Release()
{
    ULONG returnValue = InterlockedDecrement(&_RefCount);
    if (returnValue == 0)
    {
        delete this;
    }
    return returnValue;
}
