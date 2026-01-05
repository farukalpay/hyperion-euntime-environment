#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace Hyperion {

    using TermID = uint32_t;

    class Tokenizer {
    public:
        Tokenizer();
        std::unordered_map<TermID, int> Tokenize(std::string_view text);
        TermID GetTermID(std::string_view token);
        std::string GetTermString(TermID id) const;
        bool IsStopWord(std::string_view token) const;
        size_t VocabularySize() const { return m_vocab.size(); }
        const std::unordered_map<std::string, TermID>& GetVocab() const { return m_vocab; }
        const std::vector<std::string>& GetInverseVocab() const { return m_inverse_vocab; }
        void SetVocab(const std::vector<std::string>& inverse_vocab); 
    private:
        std::unordered_set<std::string> m_stopwords;
        std::unordered_map<std::string, TermID> m_vocab;
        std::vector<std::string> m_inverse_vocab;
        TermID m_next_term_id = 1; 
    };

} // namespace Hyperion
