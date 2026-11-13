#include "audio_backend.hpp"
#include <SDL.h>
#include "stb_vorbis.c"
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace
{

constexpr int kMixerRate = 48000;
constexpr int kMixerChannels = 2;

struct Snd
{
    std::vector<float> samples; // interleaved float
    int rate = 0;
    int channels = 1;
    unsigned lengthMs = 0;
    AUDIO_MODE mode = AUDIO_DEFAULT;
    int priority = 128;
};

struct Chan
{
    bool used = false;
    bool paused = true;
    bool finished = false;
    Snd *sound = nullptr;
    double pos = 0; // in samples
    float volume = 1.f;
    float pitch = 1.f;
    AUDIO_MODE mode = AUDIO_DEFAULT;
    int priority = 128;
    float minDist = 0.f, maxDist = 0.f;
    AUDIO_VECTOR pos3d{0, 0, 0};
};

struct GroupDelay
{
    unsigned long long start = 0, end = 0;
    bool hasStart = false, hasEnd = false, stopchannels = true;
};

struct Sys
{
    SDL_AudioDeviceID dev = 0;
    std::vector<Snd *> sounds;
    std::vector<std::unique_ptr<Chan>> channels;
    std::recursive_mutex mx;
    uint64_t dspClock = 0;
    GroupDelay masterDelay;
    AUDIO_VECTOR lpos{0, 0, 0}, lforward{0, 0, 1}, lup{0, 1, 0};
    float doppler = 1.f, distFactor = 1.f, rolloff = 1.f;
    bool suspended = false;
    Snd *masterSnd() { return nullptr; }
};

Sys *g_sys = nullptr;

unsigned TotalSamples(const Snd *s) { return unsigned(s->samples.size() / std::max(1, s->channels)); }

bool DecodeWav(std::vector<uint8_t> &data, Snd *out)
{
    if (data.size() < 44 || memcmp(data.data(), "RIFF", 4) != 0)
        return false;
    uint8_t *p = data.data(), *end = data.data() + data.size();
    uint8_t *cur = p + 12;
    int channels = 0, rate = 0, bits = 0, format = 0;
    std::vector<uint8_t> pcm;
    while (cur + 8 <= end)
    {
        const uint32_t id = cur[0] | cur[1] << 8 | cur[2] << 16 | uint32_t(cur[3]) << 24;
        uint32_t sz = cur[4] | cur[5] << 8 | cur[6] << 16 | uint32_t(cur[7]) << 24;
        cur += 8;
        if (id == 0x20746D66) // 'fmt '
        {
            format = cur[0] | cur[1] << 8;
            channels = cur[2] | cur[3] << 8;
            rate = cur[4] | cur[5] << 8 | cur[6] << 16 | uint32_t(cur[7]) << 24;
            bits = cur[14] | cur[15] << 8;
        }
        else if (id == 0x61746164) // 'data'
        {
            sz = std::min<uint32_t>(sz, uint32_t(end - cur));
            pcm.assign(cur, cur + sz);
            break;
        }
        cur += sz + (sz & 1);
    }
    if (!channels || !rate || pcm.empty())
        return false;
    size_t frames = 0;
    if (format == 1 && bits == 16)
    {
        frames = pcm.size() / 2 / channels;
        out->samples.resize(frames * channels);
        for (size_t i = 0; i < frames * channels; i++)
        {
            int16_t v = int16_t(pcm[i * 2] | pcm[i * 2 + 1] << 8);
            out->samples[i] = v / 32768.f;
        }
    }
    else if (format == 1 && bits == 8)
    {
        frames = pcm.size() / channels;
        out->samples.resize(frames * channels);
        for (size_t i = 0; i < frames * channels; i++)
            out->samples[i] = (pcm[i] - 128) / 128.f;
    }
    else if (format == 3 && bits == 32)
    {
        frames = pcm.size() / 4 / channels;
        out->samples.resize(frames * channels);
        memcpy(out->samples.data(), pcm.data(), frames * channels * 4);
    }
    else if (format == 1 && bits == 24)
    {
        frames = pcm.size() / 3 / channels;
        out->samples.resize(frames * channels);
        for (size_t i = 0; i < frames * channels; i++)
        {
            int v = pcm[i * 3] | pcm[i * 3 + 1] << 8 | pcm[i * 3 + 2] << 16;
            v = (v << 8) >> 8; // sign extend
            out->samples[i] = v / 8388608.f;
        }
    }
    else
    {
        return false;
    }
    out->channels = channels;
    out->rate = rate;
    out->lengthMs = unsigned(1000ull * frames / rate);
    return true;
}

bool DecodeOgg(std::vector<uint8_t> &data, Snd *out)
{
    if (data.size() < 4 || memcmp(data.data(), "OggS", 4) != 0)
        return false;
    short *decoded = nullptr;
    int channels = 0, rate = 0;
    const int n = stb_vorbis_decode_memory(data.data(), int(data.size()), &channels, &rate, &decoded);
    if (n <= 0 || !decoded || !channels || !rate)
        return false;
    out->samples.resize(size_t(n) * channels);
    for (size_t i = 0; i < out->samples.size(); i++)
        out->samples[i] = decoded[i] / 32768.f;
    free(decoded);
    out->channels = channels;
    out->rate = rate;
    out->lengthMs = unsigned(1000ull * n / rate);
    return true;
}

void MixCallback(void *, Uint8 *stream, int len)
{
    if (!g_sys)
        return;
    auto *out = reinterpret_cast<float *>(stream);
    const int frames = len / int(sizeof(float) * kMixerChannels);
    std::lock_guard<std::recursive_mutex> lock(g_sys->mx);
    static int probeCnt = 0;
    const bool probe = ++probeCnt % 94 == 0; // ~2 s at 1024 frames / 48 kHz
    float peak = 0.f;
    Sys &s = *g_sys;
    // right vector of the listener (horizontal): forward x up
    float rx = s.lforward.z;
    float rz = -s.lforward.x;
    float rlen = std::sqrt(rx * rx + rz * rz);
    if (rlen < 1e-4f)
        rlen = 1.f;
    for (int f = 0; f < frames; f++)
    {
        float l = 0.f, r = 0.f;
        const uint64_t clock = s.dspClock;
        for (auto &c : s.channels)
        {
            if (!c->used || c->paused || c->finished || !c->sound || c->sound->samples.empty())
                continue;
            if (s.masterDelay.hasStart && clock < s.masterDelay.start)
                continue;
            if (s.masterDelay.hasEnd && s.masterDelay.stopchannels && clock >= s.masterDelay.end)
            {
                c->finished = true;
                continue;
            }
            const Snd &snd = *c->sound;
            float gain = c->volume;
            float pan = 0.f;
            if ((c->mode & AUDIO_3D) && (snd.mode & AUDIO_3D))
            {
                const float dx = c->pos3d.x - s.lpos.x, dz = c->pos3d.z - s.lpos.z;
                const float dist = std::sqrt(dx * dx + dz * dz);
                float att = 1.f;
                if (c->maxDist > c->minDist && dist > c->minDist)
                    att = std::max(0.f, 1.f - (dist - c->minDist) / (c->maxDist - c->minDist));
                gain *= att;
                if (dist > 0.0001f)
                    pan = std::max(-1.f, std::min(1.f, (dx * rx + dz * rz) / (dist * rlen)));
            }
            const float panL = gain * std::sqrt(0.5f * (1.f - pan));
            const float panR = gain * std::sqrt(0.5f * (1.f + pan));
            const int nch = std::max(1, snd.channels);
            const unsigned total = TotalSamples(&snd);
            double p = c->pos;
            if (p >= total)
            {
                if ((c->mode & AUDIO_LOOP_NORMAL) || (snd.mode & AUDIO_LOOP_NORMAL))
                    p = fmod(p, total);
                else
                {
                    c->finished = true;
                    continue;
                }
            }
            const float v = snd.samples[size_t(p) * nch];
            const float v2 = nch > 1 ? snd.samples[size_t(p) * nch + 1] : v;
            l += v * panL;
            r += v2 * panR;
            // source-rate-aware stepping: a 22050 Hz sound on a 48000 Hz mixer must advance faster
            c->pos = p + double(c->pitch) * double(snd.rate) / double(kMixerRate);
        }
        out[f * 2] = std::max(-1.f, std::min(1.f, l));
        out[f * 2 + 1] = std::max(-1.f, std::min(1.f, r));
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        g_sys->dspClock++;
    }
    if (probe)
    {
        int active = 0;
        for (auto &c : s.channels)
            if (c->used && !c->paused && !c->finished)
                active++;
        fprintf(stderr, "[AUDIO-MIX] active=%d peak=%.3f delayStart=%llu%s delayEnd=%llu%s clock=%llu\n", active,
                peak, s.masterDelay.start, s.masterDelay.hasStart ? "" : "(off)", s.masterDelay.end,
                s.masterDelay.hasEnd ? "" : "(off)", (unsigned long long)s.dspClock);
    }
}

} // namespace

const char *AUDIO_ErrorString(AUDIO_RESULT) { return "SDL audio backend error"; }
namespace audio
{
AUDIO_RESULT System_Create(System **system)
{
    if (!system) return AUDIO_ERR_INVALID_PARAM;
    g_sys = new Sys();
    *system = reinterpret_cast<System *>(g_sys);
    return AUDIO_OK;
}
AUDIO_RESULT Memory_GetStats(int *current, int *peak, bool)
{
    int bytes = 0;
    if (g_sys)
        for (const auto *sound : g_sys->sounds)
            bytes += int(sound->samples.size() * sizeof(float));
    if (current) *current = bytes;
    if (peak) *peak = bytes;
    return AUDIO_OK;
}
AUDIO_RESULT System::release()
{
    close();
    if (g_sys == reinterpret_cast<Sys *>(this))
        g_sys = nullptr;
    delete this;
    return AUDIO_OK;
}

AUDIO_RESULT System::setOutput(AUDIO_OUTPUTTYPE) { return AUDIO_OK; }

AUDIO_RESULT System::setSoftwareChannels(int) { return AUDIO_OK; }

AUDIO_RESULT System::getSoftwareFormat(int *samplerate, AUDIO_SPEAKERMODE *speakermode, int *numrawspeakers)
{
    if (samplerate)
        *samplerate = kMixerRate;
    if (speakermode)
        *speakermode = AUDIO_SPEAKERMODE_STEREO;
    if (numrawspeakers)
        *numrawspeakers = kMixerChannels;
    return AUDIO_OK;
}

AUDIO_RESULT System::init(int maxchannels, AUDIO_INITFLAGS, void *)
{
    auto *s = reinterpret_cast<Sys *>(this);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        fprintf(stderr, "[AUDIO] SDL_INIT_AUDIO failed: %s\n", SDL_GetError());
        return AUDIO_ERR_OUTPUT_INIT;
    }
    SDL_AudioSpec want{}, have{};
    want.freq = kMixerRate;
    want.format = AUDIO_F32SYS;
    want.channels = kMixerChannels;
    want.samples = 1024;
    want.callback = MixCallback;
    s->dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!s->dev)
    {
        fprintf(stderr, "[AUDIO] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return AUDIO_ERR_OUTPUT_INIT;
    }
    s->channels.reserve(64);
    SDL_PauseAudioDevice(s->dev, 0);
    fprintf(stderr, "[AUDIO] SDL2 audio init ok: %dHz %dch\n", have.freq, have.channels);
    return AUDIO_OK;
}

AUDIO_RESULT System::close()
{
    auto *s = reinterpret_cast<Sys *>(this);
    if (s->dev)
    {
        SDL_PauseAudioDevice(s->dev, 1);
        SDL_CloseAudioDevice(s->dev);
        s->dev = 0;
    }
    return AUDIO_OK;
}

AUDIO_RESULT System::update() { return AUDIO_OK; }

AUDIO_RESULT System::set3DSettings(float dopplerscale, float distancefactor, float rolloffscale)
{
    auto *s = reinterpret_cast<Sys *>(this);
    s->doppler = dopplerscale;
    s->distFactor = distancefactor;
    s->rolloff = rolloffscale;
    return AUDIO_OK;
}

AUDIO_RESULT System::set3DListenerAttributes(int, const AUDIO_VECTOR *pos, const AUDIO_VECTOR *, const AUDIO_VECTOR *forward,
                                           const AUDIO_VECTOR *up)
{
    auto *s = reinterpret_cast<Sys *>(this);
    std::lock_guard<std::recursive_mutex> lock(s->mx);
    if (pos)
        s->lpos = *pos;
    if (forward)
        s->lforward = *forward;
    if (up)
        s->lup = *up;
    return AUDIO_OK;
}

AUDIO_RESULT System::get3DListenerAttributes(int, AUDIO_VECTOR *pos, AUDIO_VECTOR *vel, AUDIO_VECTOR *forward,
                                            AUDIO_VECTOR *up)
{
    auto *s = reinterpret_cast<Sys *>(this);
    if (pos)
        *pos = s->lpos;
    if (vel)
        *vel = AUDIO_VECTOR{0, 0, 0};
    if (forward)
        *forward = s->lforward;
    if (up)
        *up = s->lup;
    return AUDIO_OK;
}

AUDIO_RESULT System::mixerSuspend()
{
    auto *s = reinterpret_cast<Sys *>(this);
    if (s->dev)
        SDL_PauseAudioDevice(s->dev, 1);
    return AUDIO_OK;
}

AUDIO_RESULT System::mixerResume()
{
    auto *s = reinterpret_cast<Sys *>(this);
    if (s->dev)
        SDL_PauseAudioDevice(s->dev, 0);
    return AUDIO_OK;
}

AUDIO_RESULT System::getVersion(unsigned int *version)
{
    if (version)
        *version = AUDIO_VERSION;
    return AUDIO_OK;
}

AUDIO_RESULT System::getCPUUsage(AUDIO_CPU_USAGE *usage)
{
    if (usage)
        memset(usage, 0, sizeof(*usage));
    return AUDIO_OK;
}

AUDIO_RESULT System::createSound(const char *name_or_data, AUDIO_MODE mode, AUDIO_CREATESOUNDEXINFO *, Sound **sound)
{
    return createStream(name_or_data, mode, nullptr, sound);
}

AUDIO_RESULT System::createStream(const char *name_or_data, AUDIO_MODE mode, AUDIO_CREATESOUNDEXINFO *, Sound **sound)
{
    auto *s = reinterpret_cast<Sys *>(this);
    if (!name_or_data || !sound)
        return AUDIO_ERR_INVALID_PARAM;
    std::string path = name_or_data;
    std::replace(path.begin(), path.end(), '\\', '/');
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
    {
        fprintf(stderr, "[AUDIO] file not found: %s\n", path.c_str());
        return AUDIO_ERR_FILE_NOTFOUND;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(size);
    fread(data.data(), 1, size, f);
    fclose(f);

    auto *snd = new Snd();
    snd->mode = mode;
    if (!(DecodeWav(data, snd) || DecodeOgg(data, snd)))
    {
        fprintf(stderr, "[AUDIO] cannot decode: %s\n", path.c_str());
        delete snd;
        return AUDIO_ERR_FORMAT;
    }
    s->sounds.push_back(snd);
    *sound = reinterpret_cast<Sound *>(snd);
    fprintf(stderr, "[AUDIO] loaded '%s': %dHz %dch %ums\n", path.c_str(), snd->rate, snd->channels, snd->lengthMs);
    return AUDIO_OK;
}

AUDIO_RESULT System::playSound(Sound *sound, ChannelGroup *, bool paused, Channel **channel)
{
    auto *s = reinterpret_cast<Sys *>(this);
    std::lock_guard<std::recursive_mutex> lock(s->mx);
    if (!channel)
        return AUDIO_ERR_INVALID_PARAM;
    if (!sound)
        return AUDIO_ERR_INVALID_HANDLE;
    Chan *freeC = nullptr;
    for (auto &c : s->channels)
        if (!c->used)
        {
            freeC = c.get();
            break;
        }
    if (!freeC)
    {
        s->channels.push_back(std::make_unique<Chan>());
        freeC = s->channels.back().get();
    }
    *freeC = Chan{};
    freeC->used = true;
    freeC->paused = paused;
    freeC->finished = false;
    freeC->pos = 0;
    freeC->sound = reinterpret_cast<Snd *>(sound);
    freeC->mode = reinterpret_cast<Snd *>(sound)->mode;
    *channel = reinterpret_cast<Channel *>(freeC);
    fprintf(stderr, "[AUDIO] playSound paused=%d\n", paused);
    return AUDIO_OK;
}

AUDIO_RESULT System::getMasterChannelGroup(ChannelGroup **channelgroup)
{
    // master group is the system object itself (holds delay/clock)
    if (channelgroup)
        *channelgroup = reinterpret_cast<ChannelGroup *>(this);
    return AUDIO_OK;
}

AUDIO_RESULT Sound::release()
{
    auto *snd = reinterpret_cast<Snd *>(this);
    auto *s = g_sys;
    if (s)
    {
        std::lock_guard<std::recursive_mutex> lock(s->mx);
        s->sounds.erase(std::remove(s->sounds.begin(), s->sounds.end(), snd), s->sounds.end());
    }
    delete snd;
    return AUDIO_OK;
}

AUDIO_RESULT Sound::getLength(unsigned int *length, AUDIO_TIMEUNIT lengthtype)
{
    auto *snd = reinterpret_cast<Snd *>(this);
    if (length)
        *length = lengthtype == AUDIO_TIMEUNIT_MS ? snd->lengthMs : TotalSamples(snd);
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::stop()
{
    if (!this)
        return AUDIO_ERR_INVALID_HANDLE;
    auto *c = reinterpret_cast<Chan *>(this);
    c->used = false;
    c->finished = true;
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::setPaused(bool paused)
{
    if (!this)
        return AUDIO_ERR_INVALID_HANDLE;
    auto *c = reinterpret_cast<Chan *>(this);
    c->paused = paused;
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::getPaused(bool *p)
{
    auto *c = reinterpret_cast<Chan *>(this);
    if (p)
        *p = c->paused;
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::setVolume(float volume)
{
    if (!this)
        return AUDIO_ERR_INVALID_HANDLE;
    auto *c = reinterpret_cast<Chan *>(this);
    c->volume = volume;
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::getVolume(float *v)
{
    if (!this)
        return AUDIO_ERR_INVALID_HANDLE;
    auto *c = reinterpret_cast<Chan *>(this);
    if (v)
        *v = c->volume;
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::getAudibility(float *a)
{
    if (a)
        *a = 1;
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::setPitch(float pitch)
{
    auto *c = reinterpret_cast<Chan *>(this);
    c->pitch = pitch > 0 ? pitch : 1.f;
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::setMode(AUDIO_MODE mode)
{
    auto *c = reinterpret_cast<Chan *>(this);
    c->mode = mode;
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::getMode(AUDIO_MODE *m)
{
    auto *c = reinterpret_cast<Chan *>(this);
    if (m)
        *m = c->mode;
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::isPlaying(bool *p)
{
    auto *c = reinterpret_cast<Chan *>(this);
    if (p)
        *p = c->used && !c->finished;
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::getDSPClock(unsigned long long *dspclock, unsigned long long *parentclock)
{
    if (g_sys)
    {
        if (dspclock)
            *dspclock = g_sys->dspClock;
        if (parentclock)
            *parentclock = g_sys->dspClock;
    }
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::setDelay(unsigned long long dspclock_start, unsigned long long dspclock_end, bool stopchannels)
{
    if (g_sys)
    {
        g_sys->masterDelay.start = dspclock_start;
        g_sys->masterDelay.end = dspclock_end;
        g_sys->masterDelay.hasStart = dspclock_start != 0;
        g_sys->masterDelay.hasEnd = dspclock_end != 0;
        g_sys->masterDelay.stopchannels = stopchannels;
    }
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::addFadePoint(unsigned long long, float) { return AUDIO_OK; }

AUDIO_RESULT ChannelControl::set3DAttributes(const AUDIO_VECTOR *pos, const AUDIO_VECTOR *)
{
    auto *c = reinterpret_cast<Chan *>(this);
    if (pos)
        c->pos3d = *pos;
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::get3DAttributes(AUDIO_VECTOR *, AUDIO_VECTOR *) { return AUDIO_OK; }

AUDIO_RESULT ChannelControl::set3DMinMaxDistance(float mindistance, float maxdistance)
{
    auto *c = reinterpret_cast<Chan *>(this);
    c->minDist = mindistance;
    c->maxDist = maxdistance;
    return AUDIO_OK;
}

AUDIO_RESULT ChannelControl::get3DMinMaxDistance(float *, float *) { return AUDIO_OK; }

AUDIO_RESULT Channel::setPriority(int) { return AUDIO_OK; }

AUDIO_RESULT Channel::getPriority(int *p)
{
    if (p)
        *p = 128;
    return AUDIO_OK;
}

AUDIO_RESULT Channel::setPosition(unsigned int position, AUDIO_TIMEUNIT postype)
{
    if (!this)
        return AUDIO_ERR_INVALID_HANDLE;
    auto *c = reinterpret_cast<Chan *>(this);
    if (!c->sound || !g_sys)
        return AUDIO_OK;
    if (postype == AUDIO_TIMEUNIT_MS)
        c->pos = double(position) * c->sound->rate / 1000.0;
    return AUDIO_OK;
}

AUDIO_RESULT Channel::getPosition(unsigned int *position, AUDIO_TIMEUNIT postype)
{
    if (!this)
        return AUDIO_ERR_INVALID_HANDLE;
    auto *c = reinterpret_cast<Chan *>(this);
    if (position && c->sound && postype == AUDIO_TIMEUNIT_MS)
        *position = unsigned(double(c->pos) / c->sound->rate * 1000.0);
    return AUDIO_OK;
}

AUDIO_RESULT Channel::isVirtual(bool *v)
{
    if (v)
        *v = false;
    return AUDIO_OK;
}
}
