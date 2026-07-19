// =============================================================================
//  MatterTone POC — WavFile.h
//  Tiny self-contained 16-bit PCM WAV writer for the offline render/test harness.
//  No JUCE, no external deps — so the physics can be rendered and verified even
//  if the plugin wrapper is not built.  Header-only, C++17.
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <cmath>

namespace matter
{
inline bool writeWavMono16(const std::string& path, const std::vector<float>& samples, int sampleRate)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    const std::uint16_t channels     = 1;
    const std::uint16_t bitsPerSample = 16;
    const std::uint32_t byteRate      = (std::uint32_t) sampleRate * channels * bitsPerSample / 8;
    const std::uint16_t blockAlign    = channels * bitsPerSample / 8;
    const std::uint32_t dataBytes     = (std::uint32_t) samples.size() * blockAlign;
    const std::uint32_t riffSize      = 36 + dataBytes;

    auto w32 = [&](std::uint32_t v) { std::fputc(v & 0xff, f); std::fputc((v>>8)&0xff, f);
                                      std::fputc((v>>16)&0xff, f); std::fputc((v>>24)&0xff, f); };
    auto w16 = [&](std::uint16_t v) { std::fputc(v & 0xff, f); std::fputc((v>>8)&0xff, f); };

    std::fwrite("RIFF", 1, 4, f); w32(riffSize); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(channels);
    w32((std::uint32_t) sampleRate); w32(byteRate); w16(blockAlign); w16(bitsPerSample);
    std::fwrite("data", 1, 4, f); w32(dataBytes);

    for (float s : samples)
    {
        if (!std::isfinite(s)) s = 0.0f;          // never emit NaN into a file
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        const int v = (int) std::lround(s * 32767.0f);
        w16((std::uint16_t) (std::int16_t) v);
    }

    std::fclose(f);
    return true;
}
} // namespace matter
