#include "SpecModel.h"

#include <cmath>

#include "MiniJson.h"

namespace fourreal
{
using minijson::Json;

namespace
{
bool isPow2 (int v) { return v > 0 && (v & (v - 1)) == 0; }
}

int SpecModel::findSource (const std::string& code, int position) const
{
    for (size_t i = 0; i < sources.size(); ++i)
        if (sources[i].code == code && sources[i].position == position)
            return static_cast<int> (i);
    return -1;
}

bool SpecModel::load (const char* data, size_t size, SpecModel& out, std::string& error)
{
    try
    {
        // strtod needs a terminator; copy so we never rely on the caller's buffer being NUL-terminated.
        std::string buf (data, size);
        Json doc = minijson::Parser (buf.c_str(), buf.size()).parse();

        if (doc.at ("format").text != "spm")
            throw std::runtime_error ("not a .spm file");
        if (doc.at ("version").text.rfind ("0.", 0) != 0)
            throw std::runtime_error ("unsupported .spm version " + doc.at ("version").text);

        SpecModel m;
        const Json& meta = doc.at ("metadata");
        m.name = meta.at ("name").text;
        m.targetCode = meta.at ("target_code").text;
        m.sampleRate = meta.at ("sample_rate").number;
        m.referenceRmsDb = static_cast<float> (meta.at ("reference_rms_db").number);

        const Json& cfg = doc.at ("config");
        m.channels = static_cast<int> (cfg.at ("channels").number);
        m.kernel = static_cast<int> (cfg.at ("kernel").number);
        m.bands = static_cast<int> (cfg.at ("bands").number);
        m.nCtrl = static_cast<int> (cfg.at ("n_ctrl").number);
        m.nfft = static_cast<int> (cfg.at ("nfft").number);
        m.hop = static_cast<int> (cfg.at ("hop").number);
        m.taps = static_cast<int> (cfg.at ("taps").number);
        m.designFft = static_cast<int> (cfg.at ("design_fft").number);
        m.maxDeltaDb = static_cast<float> (cfg.at ("max_delta_db").number);
        m.eqLimitDb = static_cast<float> (cfg.at ("eq_limit_db").number);
        m.rangeLoHz = static_cast<float> (cfg.at ("range_lo_hz").number);
        m.rangeHiHz = static_cast<float> (cfg.at ("range_hi_hz").number);
        m.bandEdgesHz = cfg.at ("band_edges_hz").nums;
        const auto& dil = cfg.at ("dilations").nums;
        if (m.channels < 1 || m.channels > 64 || m.kernel != 3 || m.bands < 8 || m.bands > 512 || m.nCtrl < 2
            || ! isPow2 (m.nfft) || ! isPow2 (m.designFft) || m.hop < 1 || m.hop > m.nfft || m.taps < 8
            || m.taps > m.designFft / 2 || dil.empty() || static_cast<int> (m.bandEdgesHz.size()) != m.bands + 1)
            throw std::runtime_error ("inconsistent config");

        const size_t ch = static_cast<size_t> (m.channels);
        const size_t k = static_cast<size_t> (m.kernel);
        const Json& net = doc.at ("network");
        m.inW = minijson::tensor (net, "inp.weight", ch * static_cast<size_t> (m.bands + 1));
        m.inB = minijson::tensor (net, "inp.bias", ch);
        for (size_t b = 0; b < dil.size(); ++b)
        {
            const std::string p = "blocks." + std::to_string (b) + ".";
            SpmBlock blk;
            blk.dilation = static_cast<int> (dil[b]);
            if (blk.dilation < 1)
                throw std::runtime_error ("bad dilation");
            blk.conv = minijson::tensor (net, p + "conv.weight", 2 * ch * ch * k);
            blk.convB = minijson::tensor (net, p + "conv.bias", 2 * ch);
            blk.res = minijson::tensor (net, p + "res.weight", ch * ch);
            blk.resB = minijson::tensor (net, p + "res.bias", ch);
            blk.skip = minijson::tensor (net, p + "skip.weight", ch * ch);
            blk.skipB = minijson::tensor (net, p + "skip.bias", ch);
            m.blocks.push_back (std::move (blk));
        }
        m.headW = minijson::tensor (net, "head.weight", static_cast<size_t> (m.nCtrl) * ch);
        m.headB = minijson::tensor (net, "head.bias", static_cast<size_t> (m.nCtrl));

        for (const auto& s : doc.at ("sources").items)
        {
            SpmSourceInfo info;
            info.code = s.at ("code").text;
            info.position = static_cast<int> (s.at ("position").number);
            info.cell = s.at ("cell").text;
            m.sources.push_back (std::move (info));
        }
        m.numSources = static_cast<int> (m.sources.size());
        for (const auto& t : doc.at ("targets").items)
            m.targetCells.push_back (t.at ("cell").text);
        m.numTargets = static_cast<int> (m.targetCells.size());
        if (m.numSources < 1 || m.numTargets < 1)
            throw std::runtime_error ("no sources or targets");

        const Json& cond = doc.at ("conditioning");
        if (static_cast<int> (cond.items.size()) != m.numTargets)
            throw std::runtime_error ("conditioning: wrong number of targets");
        for (const auto& row : cond.items)
        {
            if (static_cast<int> (row.items.size()) != m.numSources)
                throw std::runtime_error ("conditioning: wrong number of sources");
            for (const auto& cell : row.items)
            {
                const Json& film = cell.at ("film");
                if (film.items.size() != m.blocks.size())
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
