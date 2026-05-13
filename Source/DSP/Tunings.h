#pragma once
#include <juce_core/juce_core.h>

namespace stemsep
{
    // MIDI note arrays are in ascending pitch order (low string first).
    struct TuningPreset
    {
        const char* id;       // stable identifier saved in state
        const char* label;    // shown in the dropdown
        std::vector<int> midi;
    };

    inline const std::vector<TuningPreset>& bassPresets()
    {
        static const std::vector<TuningPreset> p = {
            { "standard",   "Standard (E A D G)",         { 28, 33, 38, 43 } },
            { "drop_d",     "Drop D (D A D G)",           { 26, 33, 38, 43 } },
            { "drop_c",     "Drop C (C G C F)",           { 24, 31, 36, 41 } },
            { "eb",         "Half-step down (Eb)",        { 27, 32, 37, 42 } },
            { "d",          "Whole-step down (D)",        { 26, 31, 36, 41 } },
        };
        return p;
    }

    inline const std::vector<TuningPreset>& guitarPresets()
    {
        static const std::vector<TuningPreset> p = {
            { "standard",   "Standard (E A D G B E)",     { 40, 45, 50, 55, 59, 64 } },
            { "drop_d",     "Drop D (D A D G B E)",       { 38, 45, 50, 55, 59, 64 } },
            { "drop_c",     "Drop C (C G C F A D)",       { 36, 43, 48, 53, 57, 62 } },
            { "eb",         "Half-step down (Eb)",        { 39, 44, 49, 54, 58, 63 } },
            { "d",          "Whole-step down (D)",        { 38, 43, 48, 53, 57, 62 } },
            { "dadgad",     "DADGAD",                     { 38, 45, 50, 55, 57, 62 } },
        };
        return p;
    }

    // Parse a free-form tuning like "D A D G B E" or "D2 A2 D3 G3 B3 E4".
    // Octaves are optional; when omitted, each string is placed in the lowest
    // octave that is >= the previous string. The first string uses
    // defaultOctave (1 for bass, 2 for guitar) when no octave is given.
    // Returns an empty vector on parse error or wrong string count.
    inline std::vector<int> parseCustomTuning(const juce::String& text,
                                              int expectedStrings,
                                              int defaultFirstOctave)
    {
        static const int letterToPc[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
        std::vector<int> out;
        out.reserve((size_t)expectedStrings);

        const auto tokens = juce::StringArray::fromTokens(text, " \t,/-", "");
        for (const auto& rawTok : tokens)
        {
            auto tok = rawTok.trim();
            if (tok.isEmpty()) continue;
            if (tok.length() < 1) return {};

            const juce::juce_wchar letter = juce::CharacterFunctions::toUpperCase(tok[0]);
            if (letter < 'A' || letter > 'G') return {};

            int idx = 1;
            int accidental = 0;
            if (idx < tok.length())
            {
                const auto c = tok[idx];
                if (c == '#') { accidental = 1; ++idx; }
                else if (c == 'b' || c == 'B') { accidental = -1; ++idx; }
            }

            juce::String octStr = tok.substring(idx).trim();
            const bool hasOctave = octStr.isNotEmpty()
                && (octStr.containsOnly("-0123456789"));

            const int pc = (letterToPc[letter - 'A'] + accidental + 12) % 12;
            int midi;
            if (hasOctave)
            {
                const int octave = octStr.getIntValue();
                midi = (octave + 1) * 12 + pc;
            }
            else if (out.empty())
            {
                midi = (defaultFirstOctave + 1) * 12 + pc;
            }
            else
            {
                const int prev = out.back();
                const int prevOct = prev / 12 - 1;
                midi = (prevOct + 1) * 12 + pc;
                if (midi < prev) midi += 12;
            }

            if (midi < 0 || midi > 127) return {};
            out.push_back(midi);
        }

        if ((int)out.size() != expectedStrings) return {};
        return out;
    }

    inline juce::String midiListToCsv(const std::vector<int>& midi)
    {
        juce::String s;
        for (size_t i = 0; i < midi.size(); ++i)
        {
            if (i > 0) s += ",";
            s += juce::String(midi[i]);
        }
        return s;
    }
}
