// Core taxi-route logic: database of taxiway centerlines, route parsing and
// path building. Has no EuroScope or Windows dependency so it can be unit
// tested on any platform.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace taxi {

struct LatLon {
    double lat = 0.0;
    double lon = 0.0;
};

struct Runway {
    std::string name1;  // e.g. "09L"
    std::string name2;  // e.g. "27R"
    LatLon thr1;
    LatLon thr2;
};

struct Airport {
    std::string icao;
    // A taxiway may be drawn as several separate polylines.
    std::map<std::string, std::vector<std::vector<LatLon>>> taxiways;
    std::map<std::string, LatLon> points;  // holding points, stands, ...
    std::vector<Runway> runways;

    // `threshold` receives the threshold of the named end ("10L" -> start of 10L).
    bool HasRunway(const std::string& name, Runway* out = nullptr, LatLon* threshold = nullptr) const;
    bool Reference(LatLon& ref) const;  // any point of the airport, for projection
};

class TaxiDatabase {
public:
    // Loads (replacing) taxiway data from a file. Returns false if the file
    // could not be opened. Malformed lines are reported in `errors`.
    bool LoadFile(const std::string& path, std::vector<std::string>& errors);
    // Parses config text (same format as the file) and merges it.
    void LoadText(const std::string& text, std::vector<std::string>& errors);

    void ClearRunways();
    void AddRunway(const std::string& icao, const Runway& rwy);

    const Airport* Find(const std::string& icao) const;
    // Nearest airport that has taxiway data, within maxNm of pos.
    const Airport* FindNearest(const LatLon& pos, double maxNm) const;
    const std::map<std::string, Airport>& Airports() const { return airports_; }

private:
    std::map<std::string, Airport> airports_;
};

// Uppercases, turns separators (',', '.', ';') into spaces and collapses
// repeated whitespace. "a,b  c" -> "A B C".
std::string NormalizeRoute(const std::string& route);
std::vector<std::string> Tokenize(const std::string& route);

// Parses a coordinate starting at tokens[i]. Accepts EuroScope format
// ("N023.25.40.000 W046.28.30.000") or decimal ("-23.4278 -46.4750", lat lon).
// On success advances i past the pair.
bool ParseCoordPair(const std::vector<std::string>& tokens, size_t& i, LatLon& out);
bool ParseEsCoord(const std::string& s, double& value);
std::string FormatEsCoord(double value, bool isLat);

double DistanceNm(const LatLon& a, const LatLon& b);

struct PathResult {
    std::vector<LatLon> points;            // polyline to draw
    std::vector<std::string> unresolved;   // tokens not found in the database
};

// Builds the drawable path for `route` starting from the aircraft position.
// headingDeg < 0 means unknown.
PathResult BuildPath(const Airport& apt, const std::string& route, const LatLon& start,
                     double headingDeg);

}  // namespace taxi
