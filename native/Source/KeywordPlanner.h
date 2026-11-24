#pragma once
#include "IntentDsp.h"
#include <cctype>
#include <string>
#include <vector>

namespace intent {
struct Plan {
    Settings settings;
    std::string explanation;
    std::array<bool, 4> literalControls {};
    bool negated = false;
};
inline constexpr std::array<Settings, 6> genreDefaults {{
    // Warmth, punch, width, air, output dB, ceiling dBFS, guard, bypass
    {50, 65, 65, 60, -1.0f, -1.0f, false, false}, // Electronic
    {65, 70, 45, 45, -1.5f, -1.0f, false, false}, // Hip-hop
    {45, 60, 60, 65, -1.0f, -1.0f, false, false}, // Pop
    {55, 65, 45, 50, -0.5f, -1.0f, false, false}, // Rock
    {55, 30, 45, 45,  0.0f, -1.5f, false, false}, // Jazz
    {60, 20, 80, 65, -1.0f, -1.5f, false, false}, // Ambient
}};

inline int genreIndex(int genre) noexcept {
    return genre >= 0 && genre < static_cast<int>(genreDefaults.size()) ? genre : 0;
}

inline Plan makePlan(int genre, const std::string& prompt) {
    Plan result {genreDefaults[genreIndex(genre)], {}};
    auto& s = result.settings;
    std::vector<std::string> tokens;
    std::string token;
    for (unsigned char ch : prompt) {
        if (std::isalpha(ch)) token += static_cast<char>(std::tolower(ch));
        else if (!token.empty()) { tokens.push_back(token); token.clear(); }
    }
    if (!token.empty()) tokens.push_back(token);
    auto has = [&tokens](std::initializer_list<const char*> words) {
        for (const auto* word : words)
            if (std::find(tokens.begin(), tokens.end(), word) != tokens.end()) return true;
        return false;
    };
    // Fail conservatively on negation instead of applying the opposite of user intent.
    if (has({"not","no","without","never","don","avoid","unchanged","alone"})) {
        result.negated = true;
        result.explanation = "Negation detected. Genre settings applied. Adjust the controls to specify what to keep.";
        return result;
    }
    int matched=0;
    if (has({"warm","warmth","analog"})) { s.warmth+=15; result.literalControls[0]=true; ++matched; }
    if (has({"bright","airy","air"})) { s.air+=15; result.literalControls[3]=true; ++matched; }
    if (has({"dark","darker","mellow"})) { s.air-=25; result.literalControls[3]=true; ++matched; }
    if (has({"punchy","punch","impact"})) { s.punch+=15; result.literalControls[1]=true; ++matched; }
    if (has({"gentle","soft","natural"})) { s.punch-=20; result.literalControls[1]=true; ++matched; }
    if (has({"wide","expansive","spacious"})) { s.width+=15; result.literalControls[2]=true; ++matched; }
    if (has({"intimate","narrow","focused"})) { s.width-=20; result.literalControls[2]=true; ++matched; }
    s.warmth=std::clamp(s.warmth,0.0f,100.0f); s.punch=std::clamp(s.punch,0.0f,100.0f);
    s.width=std::clamp(s.width,0.0f,100.0f); s.air=std::clamp(s.air,0.0f,100.0f);
    result.explanation = matched ? "Keyword direction applied. Adjust to taste."
                                 : "No supported keywords found. Genre starting point applied; adjust to taste.";
    if (has({"bright","airy","air"}) && has({"dark","darker","mellow"}))
        result.explanation = "Bright and dark directions conflict. Air has been blended; choose the result with the Air control.";
    return result;
}
}
