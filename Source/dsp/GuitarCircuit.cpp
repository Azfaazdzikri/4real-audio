#include "GuitarCircuit.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>

namespace fourreal
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kRefInductanceH = 2.4;
const std::string kGnd = "gnd";

double taper (double x)
{
    return std::pow (std::clamp (x / 10.0, 0.0, 1.0), 2.5);
}

enum class ElemType { Resistor, Capacitor, SeriesRC, PickupShunt };

struct Elem
{
    ElemType type;
    int a; // node index, -1 = ground
    int b;
    double v1;
    double v2;
};

struct Source
{
    int node;
    double inductanceH;
    double dcrOhm;
    double gain;
};

cplx admittance (const Elem& e, cplx s)
{
    switch (e.type)
    {
        case ElemType::Resistor:    return 1.0 / e.v1;
        case ElemType::Capacitor:   return s * e.v1;
        case ElemType::SeriesRC:    return s * e.v2 / (1.0 + s * e.v2 * e.v1); // v1 = R, v2 = C
        case ElemType::PickupShunt: return 1.0 / (e.v2 + s * e.v1);            // v1 = L, v2 = R
    }
    return 0.0;
}

// Tiny nodal-analysis solver, one complex linear solve per frequency.
class Net
{
public:
    int ix (const std::string& name)
    {
        if (name == kGnd)
            return -1;
        auto it = idx_.find (name);
        if (it != idx_.end())
            return it->second;
        const int n = static_cast<int> (idx_.size());
        idx_[name] = n;
        return n;
    }

    void resistor (const std::string& a, const std::string& b, double ohm)
    {
        elems_.push_back ({ ElemType::Resistor, ix (a), ix (b), std::max (ohm, 1.0), 0.0 });
    }

    void capacitor (const std::string& a, const std::string& b, double farad)
    {
        elems_.push_back ({ ElemType::Capacitor, ix (a), ix (b), farad, 0.0 });
    }

    void seriesRC (const std::string& a, const std::string& b, double ohm, double farad)
    {
        elems_.push_back ({ ElemType::SeriesRC, ix (a), ix (b), std::max (ohm, 1.0), farad });
    }

    void pickup (const std::string& node, const Pickup& p)
    {
        const int n = ix (node);
        elems_.push_back ({ ElemType::PickupShunt, n, -1, p.inductanceH, p.dcrOhm });
        sources_.push_back ({ n, p.inductanceH, p.dcrOhm, p.gain });
    }

    std::vector<cplx> solve (const std::vector<double>& freqs, const std::string& out)
    {
        const int n = static_cast<int> (idx_.size());
        const int outIndex = idx_.at (out);
        std::vector<cplx> result (freqs.size());
        std::vector<cplx> y (static_cast<size_t> (n * n));
        std::vector<cplx> rhs (static_cast<size_t> (n));

        for (size_t k = 0; k < freqs.size(); ++k)
        {
            const cplx s (0.0, 2.0 * kPi * freqs[k]);
            std::fill (y.begin(), y.end(), cplx (0.0));
            std::fill (rhs.begin(), rhs.end(), cplx (0.0));

            for (const auto& e : elems_)
            {
                const cplx adm = admittance (e, s);
                if (e.a >= 0) y[idx (e.a, e.a, n)] += adm;
                if (e.b >= 0) y[idx (e.b, e.b, n)] += adm;
                if (e.a >= 0 && e.b >= 0)
                {
                    y[idx (e.a, e.b, n)] -= adm;
                    y[idx (e.b, e.a, n)] -= adm;
                }
            }
            for (const auto& src : sources_)
                rhs[static_cast<size_t> (src.node)] += src.gain / (src.dcrOhm + s * src.inductanceH);

            result[k] = gaussSolve (y, rhs, n)[static_cast<size_t> (outIndex)];
        }
        return result;
    }

private:
    static size_t idx (int r, int c, int n) { return static_cast<size_t> (r * n + c); }

    static std::vector<cplx> gaussSolve (std::vector<cplx> a, std::vector<cplx> b, int n)
    {
        for (int col = 0; col < n; ++col)
        {
            int pivot = col;
            for (int r = col + 1; r < n; ++r)
                if (std::abs (a[idx (r, col, n)]) > std::abs (a[idx (pivot, col, n)]))
                    pivot = r;
            if (pivot != col)
            {
                for (int c = 0; c < n; ++c)
                    std::swap (a[idx (col, c, n)], a[idx (pivot, c, n)]);
                std::swap (b[static_cast<size_t> (col)], b[static_cast<size_t> (pivot)]);
            }
            const cplx diag = a[idx (col, col, n)];
            for (int r = col + 1; r < n; ++r)
            {
                const cplx factor = a[idx (r, col, n)] / diag;
                if (factor == cplx (0.0))
                    continue;
                for (int c = col; c < n; ++c)
                    a[idx (r, c, n)] -= factor * a[idx (col, c, n)];
                b[static_cast<size_t> (r)] -= factor * b[static_cast<size_t> (col)];
            }
        }
        std::vector<cplx> x (static_cast<size_t> (n));
        for (int r = n - 1; r >= 0; --r)
        {
            cplx acc = b[static_cast<size_t> (r)];
            for (int c = r + 1; c < n; ++c)
                acc -= a[idx (r, c, n)] * x[static_cast<size_t> (c)];
            x[static_cast<size_t> (r)] = acc / a[idx (r, r, n)];
        }
        return x;
    }

    std::map<std::string, int> idx_;
    std::vector<Elem> elems_;
    std::vector<Source> sources_;
};

void addVolume (Net& net, const std::string& hot, const std::string& wiper, double potOhm, double position)
{
    const double a = taper (position);
    net.resistor (hot, wiper, (1.0 - a) * potOhm);
    net.resistor (wiper, kGnd, a * potOhm);
}

void addTone (Net& net, const std::string& node, double potOhm, double capF, double position)
{
    net.seriesRC (node, kGnd, taper (position) * potOhm, capF);
}

void addOutput (Net& net, const Rig& rig)
{
    net.capacitor ("out", kGnd, rig.cablePf * 1e-12);
    net.resistor ("out", kGnd, rig.ampInputOhm);
}
} // namespace

Pickup makePickup (double inductanceH, double dcrOhm)
{
    const double db = 10.0 * std::log10 (inductanceH / kRefInductanceH);
    return { inductanceH, dcrOhm, std::pow (10.0, db / 20.0) };
}

std::vector<cplx> stratResponse (const std::vector<double>& freqsHz, StratKind kind,
                                 const StratParams& params, const Rig& rig)
{
    constexpr double potOhm = 250e3;
    constexpr double toneCapF = 22e-9;

    const Pickup neck = makePickup (2.2, 5.8e3);
    const Pickup mid = makePickup (2.3, 5.9e3);
    const Pickup bridge = (kind == StratKind::HSS) ? makePickup (4.0, 9.0e3) : makePickup (2.6, 6.6e3);

    const int position = std::clamp (params.position, 1, 5);
    const bool useBridge = position == 1 || position == 2;
    const bool useMid = position == 2 || position == 3 || position == 4;
    const bool useNeck = position == 4 || position == 5;

    Net net;
    if (useBridge) net.pickup ("hot", bridge);
    if (useMid) net.pickup ("hot", mid);
    if (useNeck) net.pickup ("hot", neck);

    addVolume (net, "hot", "out", potOhm, params.volume);
    if (useNeck) addTone (net, "hot", potOhm, toneCapF, params.toneNeck);
    if (useMid) addTone (net, "hot", potOhm, toneCapF, params.toneMid);
    addOutput (net, rig);
    return net.solve (freqsHz, "out");
}

std::vector<cplx> lesPaulResponse (const std::vector<double>& freqsHz,
                                   const LesPaulParams& params, const Rig& rig)
{
    constexpr double potOhm = 500e3;
    constexpr double toneCapF = 22e-9;

    const Pickup neck = makePickup (4.5, 7.5e3);
    const Pickup bridge = makePickup (5.5, 8.5e3);

    const bool useNeck = params.selector == LesPaulSelector::Neck || params.selector == LesPaulSelector::Both;
    const bool useBridge = params.selector == LesPaulSelector::Bridge || params.selector == LesPaulSelector::Both;

    Net net;
    auto addChannel = [&net, potOhm, toneCapF] (const std::string& key, const Pickup& p, double vol, double tone)
    {
        const std::string hot = "hot_" + key;
        const std::string wiper = "wiper_" + key;
        net.pickup (hot, p);
        addVolume (net, hot, wiper, potOhm, vol);
        addTone (net, hot, potOhm, toneCapF, tone);
        net.resistor (wiper, "out", 1.0); // closed selector contact
    };

    if (useNeck) addChannel ("neck", neck, params.volNeck, params.toneNeck);
    if (useBridge) addChannel ("bridge", bridge, params.volBridge, params.toneBridge);
    addOutput (net, rig);
    return net.solve (freqsHz, "out");
}

} // namespace fourreal
