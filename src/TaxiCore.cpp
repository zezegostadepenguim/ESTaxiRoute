#include "TaxiCore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

namespace taxi {

namespace {

const double kPi = 3.14159265358979323846;
const double kHoldShortMeters = 70.0;  // stop this far before a runway centerline
const double kStopBarIgnoreMeters = 5.0;  // a bar this close to the start is under the aircraft

std::string Upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> SplitWhitespace(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string tok;
    while (in >> tok) out.push_back(tok);
    return out;
}

// "09L" == "9L", "RWY09L" == "09L"
std::string CanonicalRunway(std::string s) {
    s = Upper(s);
    if (s.compare(0, 3, "RWY") == 0) s = s.substr(3);
    while (s.size() > 1 && s[0] == '0') s.erase(0, 1);
    return s;
}

bool ParseDouble(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end && *end == '\0';
}

// ---- Planar geometry in meters around a reference point -------------------

struct V {
    double x = 0.0;
    double y = 0.0;
};
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y}; }
V operator*(V a, double k) { return {a.x * k, a.y * k}; }
double Dot(V a, V b) { return a.x * b.x + a.y * b.y; }
double Cross(V a, V b) { return a.x * b.y - a.y * b.x; }
double Len(V a) { return std::sqrt(Dot(a, a)); }

struct Projector {
    LatLon ref;
    double kx = 1.0;
    explicit Projector(const LatLon& r) : ref(r), kx(111320.0 * std::cos(r.lat * kPi / 180.0)) {}
    V To(const LatLon& p) const { return {(p.lon - ref.lon) * kx, (p.lat - ref.lat) * 110540.0}; }
    LatLon From(V v) const { return {v.y / 110540.0 + ref.lat, v.x / kx + ref.lon}; }
};

struct Line {
    std::vector<V> p;
    std::vector<double> cum;  // cumulative length at each vertex
    double Length() const { return cum.empty() ? 0.0 : cum.back(); }
};

Line MakeLine(const std::vector<LatLon>& pts, const Projector& proj) {
    Line l;
    for (const LatLon& ll : pts) {
        V v = proj.To(ll);
        l.cum.push_back(l.p.empty() ? 0.0 : l.cum.back() + Len(v - l.p.back()));
        l.p.push_back(v);
    }
    return l;
}

struct Hit {
    double s = 0.0;  // along-track position on the line
    V p;
    double dist = std::numeric_limits<double>::max();
};

Hit ProjectPoint(const Line& l, V q) {
    Hit best;
    if (l.p.empty()) return best;
    if (l.p.size() == 1) {
        best.p = l.p[0];
        best.dist = Len(q - l.p[0]);
        return best;
    }
    for (size_t i = 0; i + 1 < l.p.size(); ++i) {
        V a = l.p[i], b = l.p[i + 1];
        V ab = b - a;
        double len2 = Dot(ab, ab);
        double t = len2 > 0 ? std::clamp(Dot(q - a, ab) / len2, 0.0, 1.0) : 0.0;
        V p = a + ab * t;
        double d = Len(q - p);
        if (d < best.dist) {
            best.dist = d;
            best.p = p;
            best.s = l.cum[i] + t * (l.cum[i + 1] - l.cum[i]);
        }
    }
    return best;
}

V PointAt(const Line& l, double s) {
    if (l.p.size() == 1 || s <= 0) return l.p.front();
    for (size_t i = 0; i + 1 < l.p.size(); ++i) {
        if (s <= l.cum[i + 1]) {
            double seg = l.cum[i + 1] - l.cum[i];
            double t = seg > 0 ? (s - l.cum[i]) / seg : 0.0;
            return l.p[i] + (l.p[i + 1] - l.p[i]) * t;
        }
    }
    return l.p.back();
}

V TangentAt(const Line& l, double s) {
    if (l.p.size() < 2) return {0, 0};
    size_t i = 0;
    while (i + 2 < l.p.size() && s > l.cum[i + 1]) ++i;
    V d = l.p[i + 1] - l.p[i];
    double n = Len(d);
    return n > 0 ? d * (1.0 / n) : V{0, 0};
}

// Appends the part of the line between s0 and s1 (in travel order).
void AppendSub(const Line& l, double s0, double s1, std::vector<V>& out) {
    out.push_back(PointAt(l, s0));
    if (s0 <= s1) {
        for (size_t i = 0; i < l.p.size(); ++i)
            if (l.cum[i] > s0 && l.cum[i] < s1) out.push_back(l.p[i]);
    } else {
        for (size_t i = l.p.size(); i-- > 0;)
            if (l.cum[i] < s0 && l.cum[i] > s1) out.push_back(l.p[i]);
    }
    out.push_back(PointAt(l, s1));
}

// Points where line a crosses line b, as positions on a.
std::vector<Hit> Crossings(const Line& a, const Line& b) {
    std::vector<Hit> hits;
    for (size_t i = 0; i + 1 < a.p.size(); ++i) {
        V a1 = a.p[i], r = a.p[i + 1] - a.p[i];
        for (size_t j = 0; j + 1 < b.p.size(); ++j) {
            V b1 = b.p[j], s = b.p[j + 1] - b.p[j];
            double den = Cross(r, s);
            if (std::fabs(den) < 1e-9) continue;
            double t = Cross(b1 - a1, s) / den;
            double u = Cross(b1 - a1, r) / den;
            if (t < 0 || t > 1 || u < 0 || u > 1) continue;
            Hit h;
            h.p = a1 + r * t;
            h.s = a.cum[i] + t * (a.cum[i + 1] - a.cum[i]);
            h.dist = 0;
            hits.push_back(h);
        }
    }
    return hits;
}

// Point of line a closest to line b (used when the lines don't quite touch).
Hit ClosestApproach(const Line& a, const Line& b) {
    Hit best;
    for (const V& q : b.p) {
        Hit h = ProjectPoint(a, q);
        if (h.dist < best.dist) best = h;
    }
    for (size_t i = 0; i < a.p.size(); ++i) {
        Hit hb = ProjectPoint(b, a.p[i]);
        if (hb.dist < best.dist) {
            best.dist = hb.dist;
            best.p = a.p[i];
            best.s = a.cum[i];
        }
    }
    return best;
}

const Hit* NearestAlong(const std::vector<Hit>& hits, double s) {
    const Hit* best = nullptr;
    for (const Hit& h : hits)
        if (!best || std::fabs(h.s - s) < std::fabs(best->s - s)) best = &h;
    return best;
}

enum class Kind { Taxiway, Point, Runway };

struct Element {
    Kind kind;
    std::vector<Line> lines;  // taxiway polylines, or the runway centerline
    V pt;                     // point
};

}  // namespace

// ---- Airport / database ----------------------------------------------------

bool Airport::HasRunway(const std::string& name, Runway* out, LatLon* threshold) const {
    std::string want = CanonicalRunway(name);
    for (const Runway& r : runways) {
        bool first = CanonicalRunway(r.name1) == want;
        if (first || CanonicalRunway(r.name2) == want) {
            if (out) *out = r;
            if (threshold) *threshold = first ? r.thr1 : r.thr2;
            return true;
        }
    }
    return false;
}

bool Airport::Reference(LatLon& ref) const {
    for (const auto& tw : taxiways)
        for (const auto& line : tw.second)
            if (!line.empty()) {
                ref = line.front();
                return true;
            }
    if (!points.empty()) {
        ref = points.begin()->second;
        return true;
    }
    return false;
}

bool TaxiDatabase::LoadFile(const std::string& path, std::vector<std::string>& errors) {
    std::ifstream in(path);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    airports_.clear();
    LoadText(ss.str(), errors);
    return true;
}

void TaxiDatabase::LoadText(const std::string& text, std::vector<std::string>& errors) {
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    Airport* current = nullptr;
    while (std::getline(in, line)) {
        ++lineNo;
        size_t c = line.find_first_of(";#");
        if (c != std::string::npos) line.erase(c);
        std::vector<std::string> tok = SplitWhitespace(line);
        if (tok.empty()) continue;
        std::string kw = Upper(tok[0]);
        std::string where = "linha " + std::to_string(lineNo) + ": ";

        if (kw == "AIRPORT") {
            if (tok.size() < 2) {
                errors.push_back(where + "AIRPORT sem ICAO");
                continue;
            }
            std::string icao = Upper(tok[1]);
            current = &airports_[icao];
            current->icao = icao;
            continue;
        }
        if (kw != "TWY" && kw != "POINT" && kw != "STOPBAR") {
            errors.push_back(where + "palavra-chave desconhecida '" + tok[0] + "'");
            continue;
        }
        if (!current) {
            errors.push_back(where + kw + " antes de AIRPORT");
            continue;
        }
        if (tok.size() < 3) {
            errors.push_back(where + kw + " incompleto");
            continue;
        }
        std::string name = Upper(tok[1]);
        std::vector<LatLon> pts;
        size_t i = 2;
        bool ok = true;
        while (i < tok.size()) {
            LatLon p;
            if (!ParseCoordPair(tok, i, p)) {
                errors.push_back(where + "coordenada invalida '" + tok[i] + "'");
                ok = false;
                break;
            }
            pts.push_back(p);
        }
        if (!ok) continue;
        if (kw == "POINT") {
            if (pts.size() != 1) {
                errors.push_back(where + "POINT precisa de exatamente uma coordenada");
                continue;
            }
            current->points[name] = pts[0];
        } else if (kw == "STOPBAR") {
            if (pts.size() < 2) {
                errors.push_back(where + "STOPBAR precisa de pelo menos duas coordenadas");
                continue;
            }
            current->stopbars[name].push_back(pts);
        } else {
            if (pts.size() < 2) {
                errors.push_back(where + "TWY precisa de pelo menos duas coordenadas");
                continue;
            }
            current->taxiways[name].push_back(pts);
        }
    }
}

void TaxiDatabase::ClearRunways() {
    for (auto& a : airports_) a.second.runways.clear();
}

void TaxiDatabase::AddRunway(const std::string& icao, const Runway& rwy) {
    auto it = airports_.find(Upper(icao));
    if (it != airports_.end()) it->second.runways.push_back(rwy);
}

const Airport* TaxiDatabase::Find(const std::string& icao) const {
    auto it = airports_.find(Upper(icao));
    return it == airports_.end() ? nullptr : &it->second;
}

const Airport* TaxiDatabase::FindNearest(const LatLon& pos, double maxNm) const {
    const Airport* best = nullptr;
    double bestDist = maxNm;
    for (const auto& a : airports_) {
        LatLon ref;
        if (!a.second.Reference(ref)) continue;
        double d = DistanceNm(pos, ref);
        if (d <= bestDist) {
            bestDist = d;
            best = &a.second;
        }
    }
    return best;
}

// ---- Text helpers ------------------------------------------------------------

std::string NormalizeRoute(const std::string& route) {
    std::string s = Upper(route);
    for (char& c : s)
        if (c == ',' || c == '.' || c == ';' || c == '\t') c = ' ';
    std::vector<std::string> tok = SplitWhitespace(s);
    std::string out;
    for (const std::string& t : tok) {
        if (!out.empty()) out += ' ';
        out += t;
    }
    return out;
}

std::vector<std::string> Tokenize(const std::string& route) {
    return SplitWhitespace(NormalizeRoute(route));
}

bool ParseEsCoord(const std::string& s, double& value) {
    if (s.size() < 2) return false;
    char h = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    if (h != 'N' && h != 'S' && h != 'E' && h != 'W') return false;
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 1; i < s.size(); ++i) {
        if (s[i] == '.') {
            parts.push_back(cur);
            cur.clear();
        } else if (std::isdigit(static_cast<unsigned char>(s[i]))) {
            cur += s[i];
        } else {
            return false;
        }
    }
    parts.push_back(cur);
    if (parts.size() < 3 || parts.size() > 4) return false;
    for (const std::string& p : parts)
        if (p.empty()) return false;
    double deg = std::atof(parts[0].c_str());
    double min = std::atof(parts[1].c_str());
    double sec = std::atof(parts[2].c_str());
    if (parts.size() == 4) sec += std::atof(("0." + parts[3]).c_str());
    if (min >= 60 || sec >= 60) return false;
    value = deg + min / 60.0 + sec / 3600.0;
    if (h == 'S' || h == 'W') value = -value;
    return true;
}

std::string FormatEsCoord(double value, bool isLat) {
    char h = isLat ? (value < 0 ? 'S' : 'N') : (value < 0 ? 'W' : 'E');
    double v = std::fabs(value);
    long long ms = std::llround(v * 3600.0 * 1000.0);
    int deg = static_cast<int>(ms / 3600000);
    ms -= static_cast<long long>(deg) * 3600000;
    int min = static_cast<int>(ms / 60000);
    ms -= static_cast<long long>(min) * 60000;
    int sec = static_cast<int>(ms / 1000);
    ms -= static_cast<long long>(sec) * 1000;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%c%03d.%02d.%02d.%03d", h, deg, min, sec, static_cast<int>(ms));
    return buf;
}

bool ParseCoordPair(const std::vector<std::string>& tokens, size_t& i, LatLon& out) {
    if (i + 1 >= tokens.size()) return false;
    const std::string& a = tokens[i];
    const std::string& b = tokens[i + 1];
    char h = static_cast<char>(std::toupper(static_cast<unsigned char>(a[0])));
    if (h == 'N' || h == 'S') {
        char hb = static_cast<char>(std::toupper(static_cast<unsigned char>(b[0])));
        if (hb != 'E' && hb != 'W') return false;
        if (!ParseEsCoord(a, out.lat) || !ParseEsCoord(b, out.lon)) return false;
    } else {
        if (!ParseDouble(a, out.lat) || !ParseDouble(b, out.lon)) return false;
        if (std::fabs(out.lat) > 90 || std::fabs(out.lon) > 180) return false;
    }
    i += 2;
    return true;
}

double DistanceNm(const LatLon& a, const LatLon& b) {
    double la1 = a.lat * kPi / 180, la2 = b.lat * kPi / 180;
    double dla = la2 - la1, dlo = (b.lon - a.lon) * kPi / 180;
    double h = std::sin(dla / 2) * std::sin(dla / 2) +
               std::cos(la1) * std::cos(la2) * std::sin(dlo / 2) * std::sin(dlo / 2);
    return 2 * 3440.065 * std::asin(std::min(1.0, std::sqrt(h)));
}

// ---- Path building -----------------------------------------------------------

PathResult BuildPath(const Airport& apt, const std::string& route, const LatLon& start,
                     double headingDeg, bool respectStopBars) {
    PathResult result;
    LatLon ref = start;
    apt.Reference(ref);
    Projector proj(ref);

    std::vector<Element> elems;
    std::vector<std::string> tokens = Tokenize(route);
    for (const std::string& tok : tokens) {
        bool isBar = apt.stopbars.count(tok) > 0;  // naming a bar clears it
        auto tw = apt.taxiways.find(tok);
        if (tw != apt.taxiways.end()) {
            Element e{Kind::Taxiway, {}, {}};
            for (const auto& pts : tw->second) e.lines.push_back(MakeLine(pts, proj));
            elems.push_back(e);
            continue;
        }
        auto pt = apt.points.find(tok);
        if (pt != apt.points.end()) {
            elems.push_back(Element{Kind::Point, {}, proj.To(pt->second)});
            continue;
        }
        Runway rwy;
        LatLon thr;
        if (apt.HasRunway(tok, &rwy, &thr)) {
            Element e{Kind::Runway, {}, proj.To(thr)};  // pt = threshold of the named end
            e.lines.push_back(MakeLine({rwy.thr1, rwy.thr2}, proj));
            elems.push_back(e);
            continue;
        }
        if (!isBar) result.unresolved.push_back(tok);
    }

    std::vector<V> path;
    V cur = proj.To(start);
    path.push_back(cur);
    V travel{0, 0};  // direction of travel at the end of the previous taxiway
    bool firstTaxiway = true;

    for (size_t idx = 0; idx < elems.size(); ++idx) {
        const Element& e = elems[idx];
        if (e.kind == Kind::Point) {
            if (Len(e.pt - path.back()) > 0.01) travel = e.pt - path.back();
            path.push_back(e.pt);
            cur = e.pt;
            continue;
        }
        if (e.kind == Kind::Runway) continue;  // crossing, or handled as the target below

        // Taxiway: use the polyline closest to where we are.
        const Line* line = nullptr;
        Hit entry;
        for (const Line& l : e.lines) {
            Hit h = ProjectPoint(l, cur);
            if (!line || h.dist < entry.dist) {
                line = &l;
                entry = h;
            }
        }
        if (!line || line->p.size() < 2) continue;

        // What comes next decides where we leave this taxiway.
        const Element* next = nullptr;
        const Element* runwayTarget = nullptr;
        for (size_t j = idx + 1; j < elems.size(); ++j) {
            if (elems[j].kind == Kind::Runway) {
                if (!runwayTarget) runwayTarget = &elems[j];
                continue;
            }
            next = &elems[j];
            break;
        }

        double exitS;
        if (next && next->kind == Kind::Taxiway) {
            std::vector<Hit> hits;
            for (const Line& nl : next->lines) {
                std::vector<Hit> c = Crossings(*line, nl);
                hits.insert(hits.end(), c.begin(), c.end());
            }
            if (hits.empty())
                for (const Line& nl : next->lines) hits.push_back(ClosestApproach(*line, nl));
            exitS = NearestAlong(hits, entry.s)->s;
        } else if (next && next->kind == Kind::Point) {
            exitS = ProjectPoint(*line, next->pt).s;
        } else if (runwayTarget) {
            // Hold at the end of the runway that was asked for: "A 10L" goes to the
            // 10L threshold side even when A is parallel to the runway.
            const Line& rl = runwayTarget->lines.front();
            V thr = runwayTarget->pt;
            std::vector<Hit> hits = Crossings(*line, rl);
            if (hits.empty()) {
                exitS = ProjectPoint(*line, thr).s;
            } else {
                const Hit* best = &hits.front();
                for (const Hit& h : hits)
                    if (Len(h.p - thr) < Len(best->p - thr)) best = &h;
                double s = best->s;
                exitS = s >= entry.s ? std::max(entry.s, s - kHoldShortMeters)
                                     : std::min(entry.s, s + kHoldShortMeters);
            }
        } else {
            // Last taxiway with no target: continue in the direction of travel.
            V dir = travel;
            if (firstTaxiway && headingDeg >= 0) {
                double h = headingDeg * kPi / 180.0;
                dir = {std::sin(h), std::cos(h)};
            }
            double n = Len(dir);
            double along = n > 0 ? Dot(TangentAt(*line, entry.s), dir * (1.0 / n)) : 0.0;
            if (std::fabs(along) > 0.3)
                exitS = along > 0 ? line->Length() : 0.0;
            else
                exitS = (line->Length() - entry.s >= entry.s) ? line->Length() : 0.0;
        }

        AppendSub(*line, entry.s, exitS, path);
        if (std::fabs(exitS - entry.s) > 1.0) {
            V a = PointAt(*line, exitS);
            V b = PointAt(*line, exitS > entry.s ? std::max(entry.s, exitS - 5.0)
                                                 : std::min(entry.s, exitS + 5.0));
            travel = a - b;
        }
        cur = PointAt(*line, exitS);
        firstTaxiway = false;
    }

    if (respectStopBars) {
        // Cut the path at the first stop bar it crosses that the route doesn't clear.
        Line walked;
        for (const V& v : path) {
            walked.cum.push_back(walked.p.empty() ? 0.0 : walked.cum.back() + Len(v - walked.p.back()));
            walked.p.push_back(v);
        }
        const Hit* first = nullptr;
        std::vector<Hit> hits;
        std::vector<std::pair<std::string, const std::vector<LatLon>*>> owners;
        for (const auto& bar : apt.stopbars) {
            if (std::find(tokens.begin(), tokens.end(), bar.first) != tokens.end()) continue;
            for (const auto& pts : bar.second) {
                for (const Hit& h : Crossings(walked, MakeLine(pts, proj))) {
                    if (h.s < kStopBarIgnoreMeters) continue;
                    hits.push_back(h);
                    owners.push_back({bar.first, &pts});
                }
            }
        }
        size_t firstIdx = 0;
        for (size_t i = 0; i < hits.size(); ++i)
            if (!first || hits[i].s < first->s) {
                first = &hits[i];
                firstIdx = i;
            }
        if (first) {
            std::vector<V> cut;
            AppendSub(walked, 0.0, first->s, cut);
            path = cut;
            result.stopBar = owners[firstIdx].first;
            result.stopBarLine = *owners[firstIdx].second;
        }
    }

    // Drop consecutive duplicates.
    for (const V& v : path) {
        LatLon ll = proj.From(v);
        if (!result.points.empty()) {
            const LatLon& last = result.points.back();
            if (std::fabs(last.lat - ll.lat) < 1e-8 && std::fabs(last.lon - ll.lon) < 1e-8) continue;
        }
        result.points.push_back(ll);
    }
    if (result.points.size() < 2) result.points.clear();
    return result;
}

}  // namespace taxi
