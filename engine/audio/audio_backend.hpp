#pragma once

using AUDIO_RESULT = int;
using AUDIO_MODE = unsigned;
using AUDIO_INITFLAGS = unsigned;
using AUDIO_TIMEUNIT = unsigned;
using AUDIO_OUTPUTTYPE = int;
using AUDIO_SPEAKERMODE = int;
inline constexpr int AUDIO_OK = 0, AUDIO_ERR_INVALID_PARAM = 1, AUDIO_ERR_OUTPUT_INIT = 2,
    AUDIO_ERR_FILE_NOTFOUND = 3, AUDIO_ERR_FORMAT = 4, AUDIO_ERR_INVALID_HANDLE = 5;
inline constexpr unsigned AUDIO_VERSION = 1, AUDIO_DEFAULT = 0, AUDIO_LOOP_OFF = 1,
    AUDIO_LOOP_NORMAL = 2, AUDIO_2D = 4, AUDIO_3D = 8, AUDIO_CREATESAMPLE = 16,
    AUDIO_3D_LINEARROLLOFF = 32, AUDIO_HARDWARE = 0, AUDIO_INIT_NORMAL = 0,
    AUDIO_TIMEUNIT_MS = 1;
inline constexpr int AUDIO_OUTPUTTYPE_AUTODETECT = 0, AUDIO_SPEAKERMODE_STEREO = 2,
    AUDIO_CHANNEL_FREE = -1;
struct AUDIO_VECTOR { float x, y, z; };
struct AUDIO_CPU_USAGE { float dsp, stream, geometry, update, convolution1, convolution2; };
struct AUDIO_CREATESOUNDEXINFO;
const char *AUDIO_ErrorString(AUDIO_RESULT);

namespace audio
{
class System;
class Sound;
class Channel;
class ChannelGroup;
AUDIO_RESULT System_Create(System **);
AUDIO_RESULT Memory_GetStats(int *, int *, bool = true);
class ChannelControl
{
public:
    AUDIO_RESULT stop();
    AUDIO_RESULT setPaused(bool paused);
    AUDIO_RESULT getPaused(bool *p);
    AUDIO_RESULT setVolume(float volume);
    AUDIO_RESULT getVolume(float *v);
    AUDIO_RESULT getAudibility(float *a);
    AUDIO_RESULT setPitch(float pitch);
    AUDIO_RESULT setMode(AUDIO_MODE mode);
    AUDIO_RESULT getMode(AUDIO_MODE *m);
    AUDIO_RESULT isPlaying(bool *p);
    AUDIO_RESULT getDSPClock(unsigned long long *dspclock, unsigned long long *parentclock);
    AUDIO_RESULT setDelay(unsigned long long dspclock_start, unsigned long long dspclock_end, bool stopchannels);
    AUDIO_RESULT addFadePoint(unsigned long long, float);
    AUDIO_RESULT set3DAttributes(const AUDIO_VECTOR *pos, const AUDIO_VECTOR *);
    AUDIO_RESULT get3DAttributes(AUDIO_VECTOR *, AUDIO_VECTOR *);
    AUDIO_RESULT set3DMinMaxDistance(float mindistance, float maxdistance);
    AUDIO_RESULT get3DMinMaxDistance(float *, float *);
};
class Channel : public ChannelControl
{
public:
    AUDIO_RESULT setPriority(int);
    AUDIO_RESULT getPriority(int *p);
    AUDIO_RESULT setPosition(unsigned int position, AUDIO_TIMEUNIT postype);
    AUDIO_RESULT getPosition(unsigned int *position, AUDIO_TIMEUNIT postype);
    AUDIO_RESULT isVirtual(bool *v);
};
class Sound
{
public:
    AUDIO_RESULT release();
    AUDIO_RESULT getLength(unsigned int *length, AUDIO_TIMEUNIT lengthtype);
};
class System
{
public:
    AUDIO_RESULT release();
    AUDIO_RESULT setOutput(AUDIO_OUTPUTTYPE);
    AUDIO_RESULT setSoftwareChannels(int);
    AUDIO_RESULT getSoftwareFormat(int *samplerate, AUDIO_SPEAKERMODE *speakermode, int *numrawspeakers);
    AUDIO_RESULT init(int maxchannels, AUDIO_INITFLAGS, void *);
    AUDIO_RESULT close();
    AUDIO_RESULT update();
    AUDIO_RESULT set3DSettings(float dopplerscale, float distancefactor, float rolloffscale);
    AUDIO_RESULT set3DListenerAttributes(int, const AUDIO_VECTOR *pos, const AUDIO_VECTOR *, const AUDIO_VECTOR *forward,
                                           const AUDIO_VECTOR *up);
    AUDIO_RESULT get3DListenerAttributes(int, AUDIO_VECTOR *pos, AUDIO_VECTOR *vel, AUDIO_VECTOR *forward,
                                            AUDIO_VECTOR *up);
    AUDIO_RESULT mixerSuspend();
    AUDIO_RESULT mixerResume();
    AUDIO_RESULT getVersion(unsigned int *version);
    AUDIO_RESULT getCPUUsage(AUDIO_CPU_USAGE *usage);
    AUDIO_RESULT createSound(const char *name_or_data, AUDIO_MODE mode, AUDIO_CREATESOUNDEXINFO *, Sound **sound);
    AUDIO_RESULT createStream(const char *name_or_data, AUDIO_MODE mode, AUDIO_CREATESOUNDEXINFO *, Sound **sound);
    AUDIO_RESULT playSound(Sound *sound, ChannelGroup *, bool paused, Channel **channel);
    AUDIO_RESULT getMasterChannelGroup(ChannelGroup **channelgroup);
};
class ChannelGroup : public ChannelControl {};
}
