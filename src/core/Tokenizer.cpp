#include "../../include/core/Tokenizer.hpp"
#include <cctype>
#include <algorithm>

namespace Hyperion {

    // --- Tokenizer Implementation ---

    Tokenizer::Tokenizer() {
        // Basic stopwords
        std::vector<std::string> stops = {
            "the", "of", "and", "a", "to", "in", "is", "you", "that", "it", 
            "he", "was", "for", "on", "are", "as", "with", "his", "they", "i"
        };
        for (const auto& s : stops) m_stopwords.insert(s);
    }

    std::unordered_map<TermID, int> Tokenizer::Tokenize(std::string_view text) {
        std::unordered_map<TermID, int> counts;
        std::string current_token;
        current_token.reserve(32);

        for (char c : text) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                current_token.push_back(std::tolower(static_cast<unsigned char>(c)));
            } else if (!current_token.empty()) {
                // Process token
                if (!IsStopWord(current_token)) {
                    counts[GetTermID(current_token)]++;
                }
                current_token.clear();
            }
        }
        // Last token
        if (!current_token.empty() && !IsStopWord(current_token)) {
             counts[GetTermID(current_token)]++;
        }

        return counts;
    }

    TermID Tokenizer::GetTermID(std::string_view token) {
        std::string s(token);
        if (m_vocab.find(s) == m_vocab.end()) {
            m_vocab[s] = m_next_term_id;
            if (m_inverse_vocab.size() <= m_next_term_id) {
                m_inverse_vocab.resize(m_next_term_id + 100);
            }
            m_inverse_vocab[m_next_term_id] = s;
            m_next_term_id++;
        }
        return m_vocab[s];
    }

    std::string Tokenizer::GetTermString(TermID id) const {
        if (id < m_inverse_vocab.size()) {
            return m_inverse_vocab[id];
        }
        return "UNKNOWN";
    }

    bool Tokenizer::IsStopWord(std::string_view token) const {
        return m_stopwords.contains(std::string(token));
    }

    void Tokenizer::SetVocab(const std::vector<std::string>& inverse_vocab) {
        m_inverse_vocab = inverse_vocab;
        m_vocab.clear();
        m_next_term_id = 1;
        for (size_t i = 1; i < inverse_vocab.size(); ++i) {
            if (!inverse_vocab[i].empty()) {
                m_vocab[inverse_vocab[i]] = i;
                if (i >= m_next_term_id) m_next_term_id = i + 1;
            }
        }
    }

} // namespace Hyperion
