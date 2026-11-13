#pragma once

#if defined(__x86_64__) || defined(__SSE__)
#include <xmmintrin.h>
#else

// Portable (arm64/neon-friendly) replacements for the SSE4 helpers used by the sea renderer.
struct __m128
{
    float v[4];
};

#ifndef _MM_SHUFFLE
#define _MM_SHUFFLE(z, y, x, w) (((z) << 6) | ((y) << 4) | ((x) << 2) | (w))
#endif

static inline void SSE_GatherFourFloats(float *pf0, float *pf1, float *pf2, float *pf3, __m128 *pm128Result)
{
    pm128Result->v[0] = *pf0;
    pm128Result->v[1] = *pf1;
    pm128Result->v[2] = *pf2;
    pm128Result->v[3] = *pf3;
}

static inline void SSE_ScatterFourFloats(float *pf0, float *pf1, float *pf2, float *pf3, __m128 m128Value)
{
    *pf0 = m128Value.v[0];
    *pf1 = m128Value.v[1];
    *pf2 = m128Value.v[2];
    *pf3 = m128Value.v[3];
}

static inline void SSE_Normalise(__m128 m128X, __m128 m128Y, __m128 m128Z, __m128 *pm128X, __m128 *pm128Y,
                                 __m128 *pm128Z)
{
    const float len = sqrtf(m128X.v[0] * m128X.v[0] + m128Y.v[0] * m128Y.v[0] + m128Z.v[0] * m128Z.v[0]);
    const float inv = len > 0.0f ? 1.0f / len : 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        pm128X->v[i] = m128X.v[i] * inv;
        pm128Y->v[i] = m128Y.v[i] * inv;
        pm128Z->v[i] = m128Z.v[i] * inv;
    }
}

static inline void GatherFourFloats(float *pf0, float *pf1, float *pf2, float *pf3, __m128 *pm128Result)
{
    SSE_GatherFourFloats(pf0, pf1, pf2, pf3, pm128Result);
}

static inline void ScatterFourFloats(float *pf0, float *pf1, float *pf2, float *pf3, __m128 m128Value)
{
    SSE_ScatterFourFloats(pf0, pf1, pf2, pf3, m128Value);
}

static inline void Normalise(__m128 m128X, __m128 m128Y, __m128 m128Z, __m128 *pm128X, __m128 *pm128Y, __m128 *pm128Z)
{
    SSE_Normalise(m128X, m128Y, m128Z, pm128X, pm128Y, pm128Z);
}

#endif
