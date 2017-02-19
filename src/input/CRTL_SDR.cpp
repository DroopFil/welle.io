/*
 *    Copyright (C) 2017
 *    Albrecht Lohofener (albrechtloh@gmx.de)
 *
 *    This file is based on SDR-J
 *    Copyright (C) 2010, 2011, 2012, 2013
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *
 *    This file is part of the welle.io.
 *    Many of the ideas as implemented in welle.io are derived from
 *    other work, made available through the GNU general Public License.
 *    All copyrights of the original authors are recognized.
 *
 *    welle.io is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    welle.io is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with welle.io; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "CRTL_SDR.h"
#include "rtl-sdr.h"
#include <QThread>

#ifdef __MINGW32__
#define GETPROCADDRESS GetProcAddress
#else
#define GETPROCADDRESS dlsym
#endif

#define READLEN_DEFAULT 8192
//
//	For the callback, we do need some environment which
//	is passed through the ctx parameter
//
//	This is the user-side call back function
//	ctx is the calling task
static void RTLSDRCallBack(uint8_t* buf, uint32_t len, void* ctx)
{
    CRTL_SDR* theStick = (CRTL_SDR*)ctx;
    int32_t tmp;

    if ((theStick == NULL) || (len != READLEN_DEFAULT))
        return;

    tmp = theStick->_I_Buffer->putDataIntoBuffer(buf, len);
    if ((len - tmp) > 0)
        theStick->sampleCounter += len - tmp;
    theStick->_I_ShadowBuffer->putDataIntoBuffer(buf, len);
}
//
//	for handling the events in libusb, we need a controlthread
//	whose sole purpose is to process the rtlsdr_read_async function
//	from the lib.
class dll_driver : public QThread {
private:
    CRTL_SDR* theStick;

public:
    dll_driver(CRTL_SDR* d)
    {
        theStick = d;
        start();
    }

    ~dll_driver(void) {}

private:
    virtual void run(void)
    {
        (theStick->rtlsdr_read_async)(theStick->device,
            (rtlsdr_read_async_cb_t)&RTLSDRCallBack,
            (void*)theStick, 0, READLEN_DEFAULT);
    }
};

//	Our wrapper is a simple classs
CRTL_SDR::CRTL_SDR(QSettings* s, bool* success)
{
    int16_t deviceCount;
    int32_t r;
    int16_t deviceIndex;
    int16_t i;
    QString temp;

    dabstickSettings = s;
    *success = false; // just the default

    inputRate = 2048000;
    libraryLoaded = false;
    open = false;
    _I_Buffer = NULL;
    _I_ShadowBuffer = NULL;
    workerHandle = NULL;
    lastFrequency = KHz(94700); // just a dummy
    this->sampleCounter = 0;
    this->vfoOffset = 0;
    gains = NULL;

#ifdef __MINGW32__
    const char* libraryString = "rtlsdr.dll";
    Handle = LoadLibrary((wchar_t*)L"rtlsdr.dll");
#else
    const char* libraryString = "librtlsdr.so";
    Handle = dlopen("librtlsdr.so", RTLD_NOW);
#endif

    if (Handle == NULL) {
        fprintf(stderr, "failed to open %s\n", libraryString);
        return;
    }

    libraryLoaded = true;
    if (!load_rtlFunctions())
        goto err;
    //
    //	Ok, from here we have the library functions accessible
    deviceCount = this->rtlsdr_get_device_count();
    if (deviceCount == 0) {
        fprintf(stderr, "No devices found\n");
        return;
    }

    deviceIndex = 0; // default
    if (deviceCount > 1) {
        // Add handle code here
    }

    //	OK, now open the hardware
    r = this->rtlsdr_open(&device, deviceIndex);
    if (r < 0) {
        fprintf(stderr, "Opening dabstick failed\n");
        *success = false;
        return;
    }
    open = true;
    r = this->rtlsdr_set_sample_rate(device, inputRate);
    if (r < 0) {
        fprintf(stderr, "Setting samplerate failed\n");
        *success = false;
        return;
    }

    r = this->rtlsdr_get_sample_rate(device);
    fprintf(stderr, "samplerate set to %d\n", r);

    gainsCount = rtlsdr_get_tuner_gains(device, NULL);
    fprintf(stderr, "Supported gain values (%d): ", gainsCount);
    gains = new int[gainsCount];
    gainsCount = rtlsdr_get_tuner_gains(device, gains);
    for (i = gainsCount; i > 0; i--) {
        fprintf(stderr, "%.1f ", gains[i - 1] / 10.0);
    }
    fprintf(stderr, "\n");

    rtlsdr_set_tuner_gain_mode(device, 1);

    _I_Buffer = new RingBuffer<uint8_t>(1024 * 1024);
    _I_ShadowBuffer = new RingBuffer<uint8_t>(8192);

    theGain = gains[gainsCount / 2]; // default
    dabstickSettings->beginGroup("dabstickSettings");
    temp = dabstickSettings->value("externalGain", "10").toString();
    theGain = temp.toInt();
    rtlsdr_set_tuner_gain(device, theGain);

    temp = dabstickSettings->value("autogain", "autogain off").toString();
    rtlsdr_set_tuner_gain_mode(device, temp == "autogain off" ? 0 : 1);

    dabstickSettings->endGroup();

    // Enable AGC
    setAgc(true);

    *success = true;
    return;

err:
    if (open)
        this->rtlsdr_close(device);
#ifdef __MINGW32__
    FreeLibrary(Handle);
#else
    dlclose(Handle);
#endif
    libraryLoaded = false;
    open = false;
    *success = false;
    return;
}

CRTL_SDR::~CRTL_SDR(void)
{
    dabstickSettings->beginGroup("dabstickSettings");
    dabstickSettings->setValue("externalGain", theGain);
    dabstickSettings->endGroup();
    if (workerHandle != NULL) { // we are running
        this->rtlsdr_cancel_async(device);
        if (workerHandle != NULL) {
            while (!workerHandle->isFinished())
                usleep(100);
            delete workerHandle;
        }
    }
    workerHandle = NULL;
    if (open)
        this->rtlsdr_close(device);
    if (Handle != NULL)
#ifdef __MINGW32__
        FreeLibrary(Handle);
#else
        dlclose(Handle);
#endif
    if (_I_Buffer != NULL)
        delete _I_Buffer;

    if (_I_ShadowBuffer != NULL)
        delete _I_ShadowBuffer;

    if (gains != NULL)
        delete[] gains;
    open = false;;
}

void CRTL_SDR::setVFOFrequency(int32_t f)
{
    lastFrequency = f;
    (void)(this->rtlsdr_set_center_freq(device, f + vfoOffset));
}

void CRTL_SDR::getVFOFrequency(int32_t* f)
{
    *f = (int32_t)(this->rtlsdr_get_center_freq(device)) - vfoOffset;
}
//
//
bool CRTL_SDR::restartReader(void)
{
    int32_t r;

    if (workerHandle != NULL)
        return true;

    _I_Buffer->FlushRingBuffer();
    _I_ShadowBuffer->FlushRingBuffer();
    r = this->rtlsdr_reset_buffer(device);
    if (r < 0)
        return false;

    this->rtlsdr_set_center_freq(device, lastFrequency + vfoOffset);
    workerHandle = new dll_driver(this);
    //	rtlsdr_set_tuner_gain_mode (device,
    //                combo_autogain -> currentText () == "autogain on" ? 1 : 0);
    //	rtlsdr_set_tuner_gain (device, theGain);
    //	fprintf (stderr, "the gain is set to %d\n", theGain);
    return true;
}

void CRTL_SDR::stopReader(void)
{
    if (workerHandle == NULL)
        return;

    this->rtlsdr_cancel_async(device);
    if (workerHandle != NULL) {
        while (!workerHandle->isFinished())
            usleep(100);

        delete workerHandle;
    }

    workerHandle = NULL;
}
//
//	when selecting  the gain from a table, use the table value
void CRTL_SDR::setGain(const QString& gain)
{
    theGain = gain.toInt();
    rtlsdr_set_tuner_gain(device, gain.toInt());
}
//
//	when selecting with an integer in the range 0 .. 100
//	first find the table value
void CRTL_SDR::setGain(int32_t g)
{
    theGain = gains[g * gainsCount / 100];
    rtlsdr_set_tuner_gain(device, theGain);
}

void CRTL_SDR::set_autogain(const QString& autogain)
{
    rtlsdr_set_tuner_gain_mode(device, autogain == "autogain off" ? 0 : 1);
}

void CRTL_SDR::setAgc(bool b)
{
    if (b == true) {
        rtlsdr_set_tuner_gain_mode(device, 0);
        fprintf(stderr, "AGC is on\n");
    } else {
        rtlsdr_set_tuner_gain_mode(device, 1);
        fprintf(stderr, "AGC is off\n");
    }
}

//
//	correction is in Hz
void CRTL_SDR::set_fCorrection(int32_t ppm)
{
    this->rtlsdr_set_freq_correction(device, ppm);
}

void CRTL_SDR::set_KhzOffset(int32_t o)
{
    vfoOffset = Khz(o);
    (void)(this->rtlsdr_set_center_freq(device, lastFrequency + vfoOffset));
}

//
//	The brave old getSamples. For the dab stick, we get
//	size samples: still in I/Q pairs, but we have to convert the data from
//	uint8_t to DSPCOMPLEX *
int32_t CRTL_SDR::getSamples(DSPCOMPLEX* V, int32_t size)
{
    int32_t amount, i;
    uint8_t* tempBuffer = (uint8_t*)alloca(2 * size * sizeof(uint8_t));
    //
    amount = _I_Buffer->getDataFromBuffer(tempBuffer, 2 * size);
    for (i = 0; i < amount / 2; i++)
        V[i] = DSPCOMPLEX((float(tempBuffer[2 * i] - 128)) / 128.0,
            (float(tempBuffer[2 * i + 1] - 128)) / 128.0);
    return amount / 2;
}

//	and especially for our beloved spectrum viewer we provide
int32_t CRTL_SDR::getSamples(DSPCOMPLEX* V, int32_t size, int32_t segmentSize)
{
    int32_t amount, i;
    uint8_t* tempBuffer = (uint8_t*)alloca(2 * size * sizeof(uint8_t));
    //
    amount = _I_Buffer->getDataFromBuffer(tempBuffer, 2 * size);
    for (i = 0; i < amount / 2; i++)
        V[i] = DSPCOMPLEX((float(tempBuffer[2 * i] - 128)) / 128.0,
            (float(tempBuffer[2 * i + 1] - 128)) / 128.0);

    _I_Buffer->skipDataInBuffer(2 * (segmentSize - size));

    return amount / 2;
}

int32_t CRTL_SDR::getSamplesFromShadowBuffer(DSPCOMPLEX* V, int32_t size)
{
    int32_t amount, i;
    uint8_t* tempBuffer = (uint8_t*)alloca(2 * size * sizeof(uint8_t));
    //
    amount = _I_ShadowBuffer->getDataFromBuffer(tempBuffer, 2 * size);
    for (i = 0; i < amount / 2; i++)
        V[i] = DSPCOMPLEX((float(tempBuffer[2 * i] - 128)) / 128.0,
            (float(tempBuffer[2 * i + 1] - 128)) / 128.0);
    return amount / 2;
}

int32_t CRTL_SDR::Samples(void)
{
    return _I_Buffer->GetRingBufferReadAvailable() / 2;
}
//
uint8_t CRTL_SDR::myIdentity(void) { return DAB_STICK; }

int32_t CRTL_SDR::getSamplesMissed(void)
{
    int32_t tmp = sampleCounter;
    sampleCounter = 0;
    return tmp;
}

bool CRTL_SDR::load_rtlFunctions(void)
{
    //
    //	link the required procedures
    rtlsdr_open = (pfnrtlsdr_open)GETPROCADDRESS(Handle, "rtlsdr_open");
    if (rtlsdr_open == NULL) {
        fprintf(stderr, "Could not find rtlsdr_open\n");
        return false;
    }
    rtlsdr_close = (pfnrtlsdr_close)GETPROCADDRESS(Handle, "rtlsdr_close");
    if (rtlsdr_close == NULL) {
        fprintf(stderr, "Could not find rtlsdr_close\n");
        return false;
    }

    rtlsdr_set_sample_rate = (pfnrtlsdr_set_sample_rate)GETPROCADDRESS(
        Handle, "rtlsdr_set_sample_rate");
    if (rtlsdr_set_sample_rate == NULL) {
        fprintf(stderr, "Could not find rtlsdr_set_sample_rate\n");
        return false;
    }

    rtlsdr_get_sample_rate = (pfnrtlsdr_get_sample_rate)GETPROCADDRESS(
        Handle, "rtlsdr_get_sample_rate");
    if (rtlsdr_get_sample_rate == NULL) {
        fprintf(stderr, "Could not find rtlsdr_get_sample_rate\n");
        return false;
    }

    rtlsdr_get_tuner_gains = (pfnrtlsdr_get_tuner_gains)GETPROCADDRESS(
        Handle, "rtlsdr_get_tuner_gains");
    if (rtlsdr_get_tuner_gains == NULL) {
        fprintf(stderr, "Could not find rtlsdr_get_tuner_gains\n");
        return false;
    }

    rtlsdr_set_tuner_gain_mode = (pfnrtlsdr_set_tuner_gain_mode)GETPROCADDRESS(
        Handle, "rtlsdr_set_tuner_gain_mode");
    if (rtlsdr_set_tuner_gain_mode == NULL) {
        fprintf(stderr, "Could not find rtlsdr_set_tuner_gain_mode\n");
        return false;
    }

    rtlsdr_set_tuner_gain = (pfnrtlsdr_set_tuner_gain)GETPROCADDRESS(Handle, "rtlsdr_set_tuner_gain");
    if (rtlsdr_set_tuner_gain == NULL) {
        fprintf(stderr, "Cound not find rtlsdr_set_tuner_gain\n");
        return false;
    }

    rtlsdr_get_tuner_gain = (pfnrtlsdr_get_tuner_gain)GETPROCADDRESS(Handle, "rtlsdr_get_tuner_gain");
    if (rtlsdr_get_tuner_gain == NULL) {
        fprintf(stderr, "Could not find rtlsdr_get_tuner_gain\n");
        return false;
    }
    rtlsdr_set_center_freq = (pfnrtlsdr_set_center_freq)GETPROCADDRESS(
        Handle, "rtlsdr_set_center_freq");
    if (rtlsdr_set_center_freq == NULL) {
        fprintf(stderr, "Could not find rtlsdr_set_center_freq\n");
        return false;
    }

    rtlsdr_get_center_freq = (pfnrtlsdr_get_center_freq)GETPROCADDRESS(
        Handle, "rtlsdr_get_center_freq");
    if (rtlsdr_get_center_freq == NULL) {
        fprintf(stderr, "Could not find rtlsdr_get_center_freq\n");
        return false;
    }

    rtlsdr_reset_buffer = (pfnrtlsdr_reset_buffer)GETPROCADDRESS(Handle, "rtlsdr_reset_buffer");
    if (rtlsdr_reset_buffer == NULL) {
        fprintf(stderr, "Could not find rtlsdr_reset_buffer\n");
        return false;
    }

    rtlsdr_read_async = (pfnrtlsdr_read_async)GETPROCADDRESS(Handle, "rtlsdr_read_async");
    if (rtlsdr_read_async == NULL) {
        fprintf(stderr, "Cound not find rtlsdr_read_async\n");
        return false;
    }

    rtlsdr_get_device_count = (pfnrtlsdr_get_device_count)GETPROCADDRESS(
        Handle, "rtlsdr_get_device_count");
    if (rtlsdr_get_device_count == NULL) {
        fprintf(stderr, "Could not find rtlsdr_get_device_count\n");
        return false;
    }

    rtlsdr_cancel_async = (pfnrtlsdr_cancel_async)GETPROCADDRESS(Handle, "rtlsdr_cancel_async");
    if (rtlsdr_cancel_async == NULL) {
        fprintf(stderr, "Could not find rtlsdr_cancel_async\n");
        return false;
    }

    rtlsdr_set_direct_sampling = (pfnrtlsdr_set_direct_sampling)GETPROCADDRESS(
        Handle, "rtlsdr_set_direct_sampling");
    if (rtlsdr_set_direct_sampling == NULL) {
        fprintf(stderr, "Could not find rtlsdr_set_direct_sampling\n");
        return false;
    }

    rtlsdr_set_freq_correction = (pfnrtlsdr_set_freq_correction)GETPROCADDRESS(
        Handle, "rtlsdr_set_freq_correction");
    if (rtlsdr_set_freq_correction == NULL) {
        fprintf(stderr, "Could not find rtlsdr_set_freq_correction\n");
        return false;
    }

    rtlsdr_get_device_name = (pfnrtlsdr_get_device_name)GETPROCADDRESS(
        Handle, "rtlsdr_get_device_name");
    if (rtlsdr_get_device_name == NULL) {
        fprintf(stderr, "Could not find rtlsdr_get_device_name\n");
        return false;
    }

    fprintf(stderr, "OK, functions seem to be loaded\n");
    return true;
}

void CRTL_SDR::resetBuffer(void) { _I_Buffer->FlushRingBuffer(); }

int16_t CRTL_SDR::maxGain(void) { return gainsCount; }

int16_t CRTL_SDR::bitDepth(void) { return 8; }
