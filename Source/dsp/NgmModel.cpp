#include "NgmModel.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>

namespace fourreal
{
namespace
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
    std::vector<std::pair<std::string, Json>> members;

    const Json* find (const char* key) const
    {
        for (const auto& m : members)
            if (m.first == key)
                return &m.second;
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
                v.members.emplace_back (std::move (key), value (depth + 1));
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

std::vector<float> floats (const Json& v, size_t expected, const char* what)
{
    if (v.type != Json::Type::Array || v.nums.size() != expected)
        throw std::runtime_error (std::string ("wrong size for ") + what);
    return std::vector<float> (v.nums.begin(), v.nums.end());
}

// Weight tensor "name" -> { shape, data }.
std::vector<float> tensor (const Json& net, const std::string& name, size_t expected)
{
    const Json& t = net.at (name.c_str());
    return floats (t.at ("data"), expected, name.c_str());
}
} // namespace

int NgmModel::findSource (const std::string& code, int position) const
{
    for (size_t i = 0; i < sources.size(); ++i)
        if (sources[i].code == code && sources[i].position == position)
            return static_cast<int> (i);
    return -1;
}

int NgmModel::receptiveField() const
{
    int rf = 1;
    for (const auto& b : blocks)
        rf += (kernel - 1) * b.dilation;
    return rf;
}

bool NgmModel::load (const char* data, size_t size, NgmModel& out, std::string& error)
{
    try
    {
        // strtod needs a terminator; copy so we never rely on the caller's buffer being NUL-terminated.
        std::string buf (data, size);
        Json doc = Parser (buf.c_str(), buf.size()).parse();

        if (doc.at ("format").text != "ngm")
            throw std::runtime_error ("not an .ngm file");
        if (doc.at ("version").text.rfind ("0.", 0) != 0)
            throw std::runtime_error ("unsupported .ngm version " + doc.at ("version").text);

        NgmModel m;
        const Json& meta = doc.at ("metadata");
        m.name = meta.at ("name").text;
        m.targetCode = meta.at ("target_code").text;
        m.sampleRate = meta.at ("sample_rate").number;
        m.inputScale = static_cast<float> (meta.at ("input_scale").number);
        m.referenceRmsDb = static_cast<float> (meta.at ("reference_rms_db").number);

        const Json& cfg = doc.at ("config");
        m.channels = static_cast<int> (cfg.at ("channels").number);
        m.kernel = static_cast<int> (cfg.at ("kernel").number);
        m.numBlocks = static_cast<int> (cfg.at ("blocks").number);
        m.bands = static_cast<int> (cfg.at ("bands").number);
        const auto& dil = cfg.at ("dilations").nums;
        const auto& centers = cfg.at ("band_centers_hz").nums;
        if (m.channels < 1 || m.channels > 64 || m.kernel < 1 || m.kernel > 8 || m.numBlocks < 1
            || static_cast<int> (dil.size()) != m.numBlocks || static_cast<int> (centers.size()) != m.bands)
            throw std::runtime_error ("inconsistent config");
        m.bandCentersHz = centers;

        const size_t ch = static_cast<size_t> (m.channels);
        const size_t k = static_cast<size_t> (m.kernel);
        const Json& net = doc.at ("network");
        m.inW = tensor (net, "inp.weight", ch);
        m.inB = tensor (net, "inp.bias", ch);
        for (int b = 0; b < m.numBlocks; ++b)
        {
            const std::string p = "blocks." + std::to_string (b) + ".";
            NgmBlock blk;
            blk.dilation = static_cast<int> (dil[static_cast<size_t> (b)]);
            if (blk.dilation < 1)
                throw std::runtime_error ("bad dilation");
            blk.conv = tensor (net, p + "conv.weight", 2 * ch * ch * k);
            blk.convB = tensor (net, p + "conv.bias", 2 * ch);
            blk.res = tensor (net, p + "res.weight", ch * ch);
            blk.resB = tensor (net, p + "res.bias", ch);
            blk.skip = tensor (net, p + "skip.weight", ch * ch);
            blk.skipB = tensor (net, p + "skip.bias", ch);
            m.blocks.push_back (std::move (blk));
        }
        m.out0W = tensor (net, "out.0.weight", ch * ch);
        m.out0B = tensor (net, "out.0.bias", ch);
        m.out1W = tensor (net, "out.1.weight", ch);
        m.out1B = tensor (net, "out.1.bias", 1)[0];

        const Json& srcs = doc.at ("sources");
        for (const auto& s : srcs.items)
        {
            NgmSourceInfo info;
            info.code = s.at ("code").text;
            info.position = static_cast<int> (s.at ("position").number);
            info.cell = s.at ("cell").text;
            m.sources.push_back (std::move (info));
        }
        m.numSources = static_cast<int> (m.sources.size());
        m.numTargets = static_cast<int> (doc.at ("targets").items.size());
        if (m.numSources < 1 || m.numTargets < 1)
            throw std::runtime_error ("no sources or targets");

        const Json& cond = doc.at ("conditioning");
        if (static_cast<int> (cond.items.size()) != m.numTargets)
            throw std::runtime_error ("conditioning: wrong number of targets");
        const size_t filmPer = static_cast<size_t> (m.numBlocks) * 4 * ch;
        m.film.reserve (static_cast<size_t> (m.numTargets * m.numSources) * filmPer);
        m.eqDb.reserve (static_cast<size_t> (m.numTargets * m.numSources) * static_cast<size_t> (m.bands));
        for (const auto& row : cond.items)
        {
            if (static_cast<int> (row.items.size()) != m.numSources)
                throw std::runtime_error ("conditioning: wrong number of sources");
            for (const auto& cell : row.items)
            {
                const Json& film = cell.at ("film");
                if (static_cast<int> (film.items.size()) != m.numBlocks)
                    throw std::runtime_error ("film: wrong number of blocks");
                for (const auto& fb : film.items)
                {
                    if (fb.nums.size() != 4 * ch)
                        throw std::runtime_error ("film: wrong vector size");
                    m.film.insert (m.film.end(), fb.nums.begin(), fb.nums.end());
                }
                const Json& eq = cell.at ("eq_db");
                if (eq.nums.size() != static_cast<size_t> (m.bands))
                    throw std::runtime_error ("eq_db: wrong size");
                m.eqDb.insert (m.eqDb.end(), eq.nums.begin(), eq.nums.end());
            }
        }

        out = std::move (m);
        return true;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
}

} // namespace fourreal
