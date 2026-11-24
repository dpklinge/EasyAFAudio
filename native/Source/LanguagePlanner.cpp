#include "LanguagePlanner.h"

#if INTENT_USE_MINILM
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#include <mutex>
#include <numeric>
#include <regex>
#include <unordered_map>

namespace {
constexpr size_t embeddingSize = 384;
constexpr size_t maxTokens = 128;
using Embedding = std::array<float, embeddingSize>;

struct Bucket {
    size_t control;
    float offset;
    const char* label;
    std::array<const char*, 7> examples;
};

constexpr Bucket buckets[] = {
    {0, 15, "Warmth up", {"more warmth", "fuller bottom end", "give the bass more body",
                         "richer low frequencies", "beefier bass", "give the low end more weight", "thicken the bass"}},
    {0, -15, "Warmth down", {"less bass", "thin out the low end", "reduce bass heaviness",
                           "trim the bottom end", "tame the booming bass", "reduce low frequencies", "less bass weight"}},
    {1, 15, "Punch up", {"add punch", "more drum impact", "give the beat more snap",
                        "make the drums hit harder", "more rhythmic attack", "more bite on drum hits", "stronger drum accents"}},
    {1, -20, "Punch down", {"less compression", "preserve the transients", "let the dynamics breathe",
                           "keep the peaks intact", "more dynamic range", "keep the attack intact", "ease off compression"}},
    {2, 15, "Width up", {"wider stereo image", "spread it across the speakers", "more stereo separation",
                        "expand the soundstage", "open up the stereo field", "increase stereo width", "push the sides outward"}},
    {2, -20, "Width down", {"narrow the stereo image", "bring the sides closer to the center",
                           "less stereo spread", "center the mix", "more mono", "reduce the width of the stereo field", "pull the sides in"}},
    {3, 15, "Air up", {"more treble", "add sparkle", "bring out the high frequencies",
                      "lift the top end", "clearer and brighter highs", "boost the upper frequencies", "glossy treble"}},
    {3, -25, "Air down", {"tame harsh highs", "take the edge off the treble", "soften the top end",
                         "reduce high frequencies", "dull the sharp cymbals", "cut brittle top end", "less brightness"}},
};
constexpr const char* otherExamples[] = {"remove hiss", "make the vocals louder", "add reverb",
    "change the tempo", "less boxy midrange", "tune the vocals", "make the song louder",
    "change the key", "reduce sibilance", "more delay", "hello world", "the weather is pleasant"};

bool hasLiteralControl(const intent::Plan& plan) {
    return std::any_of(plan.literalControls.begin(), plan.literalControls.end(), [](bool set) { return set; });
}

int polarityHint(size_t control, const juce::String& clause) {
    // Sentence similarity often puts antonyms close together. Use clear directional verbs
    // to choose between the two labels for an axis already identified by the model.
    const auto text = clause.toStdString();
    static const std::regex raise(R"(\b(?:add|boost|increase|lift|amplify)\b)", std::regex::icase);
    static const std::regex lower(R"(\b(?:less|reduce|decrease|trim|tame|cut|attenuate)\b)", std::regex::icase);
    static const std::regex widthUp(R"(\b(?:stretch|expand|broaden|widen|spread)\b)", std::regex::icase);
    static const std::regex widthDown(R"(\b(?:narrow|center|centre|mono|pull (?:it|the sides) in)\b)", std::regex::icase);
    if (control == 1)
        return 0;
    if (control == 2) {
        if (std::regex_search(text, widthDown))
            return -1;
        if (std::regex_search(text, widthUp))
            return std::regex_search(text, lower) ? -1 : 1;
    }
    const bool up = std::regex_search(text, raise);
    const bool down = std::regex_search(text, lower);
    return up == down ? 0 : (up ? 1 : -1);
}

juce::File languageAssets() {
    const auto directory = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    for (const auto& candidate : {directory.getSiblingFile("Resources").getChildFile("IntentLanguage"),
                                  directory.getChildFile("IntentLanguage"),
                                  directory.getChildFile("Intent.vst3/Contents/Resources/IntentLanguage")})
        if (candidate.getChildFile("model.onnx").existsAsFile())
            return candidate;
    throw std::runtime_error("MiniLM model files are missing");
}

class MiniLm {
public:
    explicit MiniLm(const juce::File& directory) {
        if (!runtime.open(directory.getChildFile("onnxruntime.dll").getFullPathName()))
            throw std::runtime_error("Could not load the bundled ONNX Runtime");
        const auto getApi = reinterpret_cast<decltype(&OrtGetApiBase)>(runtime.getFunction("OrtGetApiBase"));
        const auto* api = getApi ? getApi()->GetApi(ORT_API_VERSION) : nullptr;
        if (api == nullptr)
            throw std::runtime_error("ONNX Runtime API version mismatch");
        Ort::InitApi(api);

        juce::StringArray lines;
        directory.getChildFile("vocab.txt").readLines(lines);
        for (int i = 0; i < lines.size(); ++i)
            vocabulary.emplace(lines[i].toStdString(), i);
        for (const auto* special : {"[PAD]", "[UNK]", "[CLS]", "[SEP]"})
            if (vocabulary.count(special) == 0)
                throw std::runtime_error("MiniLM vocabulary is incomplete");

        environment = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "Intent");
        environment.DisableTelemetryEvents();
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(2);
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
        options.AddConfigEntry("session.intra_op.allow_spinning", "0");
        const auto path = directory.getChildFile("model.onnx").getFullPathName();
        session = Ort::Session(environment, path.toWideCharPointer(), options);

        // Generate reference vectors once per process, in one batch.
        std::vector<juce::String> references;
        for (const auto& bucket : buckets)
            for (const auto* example : bucket.examples)
                references.emplace_back(example);
        for (const auto* example : otherExamples)
            references.emplace_back(example);
        referenceVectors = embed(references);
    }

    std::vector<int> classify(const std::vector<juce::String>& clauses) {
        std::lock_guard<std::mutex> guard(inferenceMutex);
        const auto vectors = embed(clauses);
        std::vector<int> labels;
        for (size_t row = 0; row < vectors.size(); ++row) {
            const auto& vector = vectors[row];
            constexpr auto bucketCount = std::size(buckets);
            std::array<float, bucketCount> scores;
            scores.fill(-1.0f);
            size_t index = 0;
            const auto similarity = [&](const auto& reference) {
                return std::inner_product(reference.begin(), reference.end(), vector.begin(), 0.0f);
            };
            for (size_t bucket = 0; bucket < bucketCount; ++bucket)
                for (size_t example = 0; example < buckets[bucket].examples.size(); ++example)
                    scores[bucket] = std::max(scores[bucket], similarity(referenceVectors[index++]));

            const auto best = std::max_element(scores.begin(), scores.end());
            auto label = static_cast<int>(std::distance(scores.begin(), best));
            const auto control = buckets[label].control;
            const auto hint = polarityHint(control, clauses[row]);
            if (hint != 0 && (buckets[label].offset > 0) != (hint > 0))
                label ^= 1; // Increase and decrease are adjacent for each control.
            const float confidence = scores[label];
            float competitor = -1;
            for (size_t i = 0; i < scores.size(); ++i)
                if (i != static_cast<size_t>(label) && (hint == 0 || buckets[i].control != control))
                    competitor = std::max(competitor, scores[i]);
            for (; index < referenceVectors.size(); ++index)
                competitor = std::max(competitor, similarity(referenceVectors[index]));
            labels.push_back(confidence >= 0.45f && confidence - competitor >= 0.03f ? label : -1);
        }
        return labels;
    }

private:
    std::vector<int64_t> tokenize(const juce::String& input) const {
        std::vector<int64_t> ids { vocabulary.at("[CLS]") };
        std::string word;
        const auto appendWord = [&] {
            if (word.empty() || ids.size() >= maxTokens - 1)
                return;
            std::vector<int> pieces;
            size_t offset = 0;
            while (offset < word.size() && word.size() <= 100) {
                auto end = word.size();
                auto piece = vocabulary.end();
                while (end > offset) {
                    piece = vocabulary.find((offset == 0 ? "" : "##") + word.substr(offset, end - offset));
                    if (piece != vocabulary.end())
                        break;
                    --end;
                }
                if (piece == vocabulary.end())
                    break;
                pieces.push_back(piece->second);
                offset = end;
            }
            if (offset != word.size())
                ids.push_back(vocabulary.at("[UNK]"));
            else
                for (const auto piece : pieces)
                    if (ids.size() < maxTokens - 1)
                        ids.push_back(piece);
            word.clear();
        };
        const auto lower = input.toLowerCase();
        for (auto characters = lower.getCharPointer(); !characters.isEmpty();) {
            const auto character = characters.getAndAdvance();
            if (juce::CharacterFunctions::isLetterOrDigit(character)) {
                word += juce::String::charToString(character).toStdString();
            } else {
                appendWord();
                if (!juce::CharacterFunctions::isWhitespace(character) && character >= 32) {
                    word = juce::String::charToString(character).toStdString();
                    appendWord();
                }
            }
            if (ids.size() >= maxTokens - 1)
                break;
        }
        appendWord();
        ids.push_back(vocabulary.at("[SEP]"));
        return ids;
    }

    std::vector<Embedding> embed(const std::vector<juce::String>& texts) {
        std::vector<std::vector<int64_t>> encoded;
        size_t length = 0;
        for (const auto& text : texts) {
            encoded.push_back(tokenize(text));
            length = std::max(length, encoded.back().size());
        }
        std::vector<int64_t> ids(texts.size() * length, vocabulary.at("[PAD]"));
        std::vector<int64_t> mask(ids.size(), 0), type(ids.size(), 0);
        for (size_t row = 0; row < encoded.size(); ++row) {
            std::copy(encoded[row].begin(), encoded[row].end(), ids.begin() + row * length);
            std::fill_n(mask.begin() + row * length, encoded[row].size(), 1);
        }
        const int64_t shape[] = { static_cast<int64_t>(texts.size()), static_cast<int64_t>(length) };
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputs[] = {
            Ort::Value::CreateTensor<int64_t>(memory, ids.data(), ids.size(), shape, 2),
            Ort::Value::CreateTensor<int64_t>(memory, mask.data(), mask.size(), shape, 2),
            Ort::Value::CreateTensor<int64_t>(memory, type.data(), type.size(), shape, 2),
        };
        const char* inputNames[] = {"input_ids", "attention_mask", "token_type_ids"};
        const char* outputNames[] = {"last_hidden_state"};
        auto outputs = session.Run(Ort::RunOptions{nullptr}, inputNames, inputs, 3, outputNames, 1);
        const auto dimensions = outputs.front().GetTensorTypeAndShapeInfo().GetShape();
        if (dimensions != std::vector<int64_t>{shape[0], shape[1], embeddingSize})
            throw std::runtime_error("Unexpected MiniLM output shape");
        const auto* data = outputs.front().GetTensorData<float>();
        std::vector<Embedding> result(texts.size(), Embedding{});
        for (size_t row = 0; row < texts.size(); ++row) {
            auto& vector = result[row];
            for (size_t token = 0; token < encoded[row].size(); ++token)
                for (size_t dim = 0; dim < embeddingSize; ++dim)
                    vector[dim] += data[(row * length + token) * embeddingSize + dim];
            const float norm = std::sqrt(std::inner_product(vector.begin(), vector.end(), vector.begin(), 0.0f));
            if (!(norm > 0) || !std::isfinite(norm))
                throw std::runtime_error("Invalid MiniLM embedding");
            // Normalizing the sum is equivalent to normalizing the masked mean.
            for (auto& value : vector)
                value /= norm;
        }
        return result;
    }

    juce::DynamicLibrary runtime;
    Ort::Env environment {nullptr};
    Ort::Session session {nullptr};
    std::unordered_map<std::string, int> vocabulary;
    std::vector<Embedding> referenceVectors;
    std::mutex inferenceMutex;
};
}
#endif

intent::Plan intent::makeLanguagePlan(int genre, const juce::String& prompt) {
    auto plan = makePlan(genre, prompt.toStdString());
#if INTENT_USE_MINILM
    if (plan.negated || prompt.trim().isEmpty())
        return plan;

    static const std::regex delimiter(R"([,;.!?\r\n]+|\s+(?:and|but)\s+)", std::regex::icase);
    static const std::regex qualified(R"(\b(?:less|reduce|tame|preserve|keep|tone down|too)\b)", std::regex::icase);
    const auto text = prompt.toStdString();
    std::vector<juce::String> clauses;
    juce::StringArray literalClauses;
    for (std::sregex_token_iterator it(text.begin(), text.end(), delimiter, -1), end; it != end; ++it) {
        const auto clause = juce::String(*it).trim();
        if (clause.isEmpty())
            continue;
        if (!std::regex_search(it->first, it->second, qualified)
            && hasLiteralControl(makePlan(genre, clause.toStdString())))
            literalClauses.add(clause);
        else if (clauses.size() < 12)
            clauses.push_back(clause);
    }
    plan = makePlan(genre, literalClauses.joinIntoString(", ").toStdString());
    if (clauses.empty())
        return plan;

    try {
        static MiniLm model(languageAssets());
        std::array<unsigned, 4> directions {};
        for (const auto label : model.classify(clauses))
            if (label >= 0) {
                const auto& bucket = buckets[label];
                directions[bucket.control] |= bucket.offset > 0 ? 1u : 2u;
            }
        float* controls[] = {&plan.settings.warmth, &plan.settings.punch, &plan.settings.width, &plan.settings.air};
        juce::StringArray applied;
        for (const auto& bucket : buckets)
            if (!plan.literalControls[bucket.control] && directions[bucket.control] == (bucket.offset > 0 ? 1u : 2u)) {
                auto& value = *controls[bucket.control];
                value = std::clamp(value + bucket.offset, 0.0f, 100.0f);
                applied.add(bucket.label);
            }
        if (!applied.isEmpty()) {
            const auto prefix = hasLiteralControl(plan) ? "Keywords and MiniLM applied. MiniLM: " : "MiniLM: ";
            plan.explanation = (prefix + applied.joinIntoString(", ")).toStdString();
        } else if (!hasLiteralControl(plan)) {
            plan.explanation = "MiniLM found no clear supported direction. Genre settings applied.";
        }
        if (std::find(directions.begin(), directions.end(), 3u) != directions.end())
            plan.explanation += " Conflicting MiniLM directions were skipped.";
    } catch (const std::exception&) {
        plan.explanation += " MiniLM unavailable. Check the bundled language files.";
    }
#endif
    return plan;
}
