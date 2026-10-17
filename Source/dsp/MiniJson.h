#pragma once

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Minimal JSON reader for the model files (.spm). Arrays whose first element is a number are stored as plain numbers
// (the weight tables hold ~100k values), everything else as nested values. No JUCE dependency.
namespace fourreal
{
namespace minijson
{
// Minimal JSON reader. Arrays whose first element is a number are stored as plain numbers (the weight
// tables hold ~100k values), everything else as nested values.
struct Json
{
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    double number = 0.0;
    std::string text;
    std::vector<double> nums;                       // numeric array
    std::vector<Json> items;                        // non-numeric array
    std::vector<std::string> keys;                  // object member names
    std::vector<Json> values;                       // object member values (same order as keys)

    const Json* find (const char* key) const
    {
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key)
                return &values[i];
        return nullptr;
    }
    const Json& at (const char* key) const
    {
        if (const auto* v = find (key))
            return *v;
        throw std::runtime_error (std::string ("missing key '") + key + "'");
    }
    size_t size() const { return type == Type::Array ? (nums.empty() ? items.size() : nums.size()) : 0; }
};

class Parser
{
public:
    Parser (const char* data, size_t size) : p_ (data), end_ (data + size) {}

    Json parse()
    {
        Json v = value (0);
        skipWs();
        if (p_ != end_)
            fail ("trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail (const char* what) const { throw std::runtime_error (std::string ("JSON: ") + what); }

    void skipWs()
    {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\n' || *p_ == '\t' || *p_ == '\r'))
            ++p_;
    }

    void expect (char c)
    {
        skipWs();
        if (p_ >= end_ || *p_ != c)
            fail ("unexpected character");
        ++p_;
    }

    std::string string()
    {
        expect ('"');
        std::string s;
        while (p_ < end_ && *p_ != '"')
        {
            char c = *p_++;
            if (c == '\\' && p_ < end_)
            {
                c = *p_++;
                switch (c)
                {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case 'u':
                        if (end_ - p_ < 4) fail ("bad \\u escape");
                        s += '?'; // names are ASCII; keep going
                        p_ += 4;
                        break;
                    default: s += c; break;
                }
            }
            else
                s += c;
        }
        if (p_ >= end_)
            fail ("unterminated string");
        ++p_;
        return s;
    }

    double number()
    {
        char* stop = nullptr;
        const double v = std::strtod (p_, &stop); // the document is NUL-terminated by the caller
        if (stop == p_)
            fail ("bad number");
        p_ = stop;
        return v;
    }

    Json value (int depth)
    {
        if (depth > 32)
            fail ("nesting too deep");
        skipWs();
        if (p_ >= end_)
            fail ("unexpected end");
        Json v;
        const char c = *p_;
        if (c == '{')
        {
            ++p_;
            v.type = Json::Type::Object;
            skipWs();
            if (p_ < end_ && *p_ == '}') { ++p_; return v; }
            for (;;)
            {
                skipWs();
                std::string key = string();
                expect (':');
                v.keys.push_back (std::move (key));
                v.values.push_back (value (depth + 1));
                skipWs();
                if (p_ < end_ && *p_ == ',') { ++p_; continue; }
                expect ('}');
                break;
            }
        }
        else if (c == '[')
        {
            ++p_;
            v.type = Json::Type::Array;
            skipWs();
            if (p_ < end_ && *p_ == ']') { ++p_; return v; }
            skipWs();
            const bool numeric = p_ < end_ && (*p_ == '-' || *p_ == '+' || (*p_ >= '0' && *p_ <= '9'));
            for (;;)
            {
                if (numeric)
                {
                    skipWs();
                    v.nums.push_back (number());
                }
                else
                    v.items.push_back (value (depth + 1));
                skipWs();
                if (p_ < end_ && *p_ == ',') { ++p_; continue; }
                expect (']');
                break;
            }
        }
        else if (c == '"')
        {
            v.type = Json::Type::String;
            v.text = string();
        }
        else if (end_ - p_ >= 4 && std::strncmp (p_, "true", 4) == 0)
        {
            v.type = Json::Type::Bool; v.number = 1.0; p_ += 4;
        }
        else if (end_ - p_ >= 5 && std::strncmp (p_, "false", 5) == 0)
        {
            v.type = Json::Type::Bool; p_ += 5;
        }
        else if (end_ - p_ >= 4 && std::strncmp (p_, "null", 4) == 0)
        {
            p_ += 4;
        }
        else
        {
            v.type = Json::Type::Number;
            v.number = number();
        }
        return v;
    }

    const char* p_;
    const char* end_;
};

inline std::vector<float> floats (const Json& v, size_t expected, const char* what)
{
    if (v.type != Json::Type::Array || v.nums.size() != expected)
        throw std::runtime_error (std::string ("wrong size for ") + what);
    return std::vector<float> (v.nums.begin(), v.nums.end());
}

// Weight tensor "name" -> { shape, data }.
inline std::vector<float> tensor (const Json& net, const std::string& name, size_t expected)
{
    const Json& t = net.at (name.c_str());
    return floats (t.at ("data"), expected, name.c_str());
}
} // namespace minijson
} // namespace fourreal
