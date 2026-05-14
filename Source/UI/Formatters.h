#pragma once
#include <juce_core/juce_core.h>

namespace stemsep
{
    inline juce::String trimTrailingZeros(juce::String s)
    {
        if (! s.containsChar('.')) return s;
        return s.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    }

    inline juce::String formatFreq(double hz)
    {
        if (hz >= 1000.0)
            return trimTrailingZeros(juce::String(hz / 1000.0, 2)) + " kHz";
        return trimTrailingZeros(juce::String(hz, 2)) + " Hz";
    }

    inline juce::String formatQ(double q)
    {
        return trimTrailingZeros(juce::String(q, 2));
    }

    inline juce::String formatGain(double db)
    {
        const juce::String num = trimTrailingZeros(juce::String(db, 2));
        if (db > 0.0)
            return "+" + num + " dB";
        return num + " dB";
    }

    inline double parseFreq(const juce::String& t)
    {
        const auto digits = t.retainCharacters("0123456789.").getDoubleValue();
        return t.containsIgnoreCase("k") ? digits * 1000.0 : digits;
    }

    inline double parseGain(const juce::String& t)
    {
        return t.retainCharacters("-+0123456789.").getDoubleValue();
    }
}
