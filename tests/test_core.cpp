// Unit tests for TaxiCore. Build: g++ -std=c++17 -Isrc tests/test_core.cpp src/TaxiCore.cpp
#include <cmath>
#include <cstdio>
#include <string>

#include "TaxiCore.h"

using namespace taxi;

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

static bool Near(double a, double b, double tol = 1e-5) { return std::fabs(a - b) < tol; }

static const char* kConfig = R"(
; test airport around 0/0
AIRPORT TEST
TWY A 0 0  0 0.01
TWY B -0.005 0.005  0.005 0.005
TWY C 0 0.009  0.005 0.009
POINT HP 0.003 0.009
; parallel to the runway, a bit closer to it at the 27 end
TWY D 0.003 0.0  0.0031 0.01
; U shape crossing the runway near both ends
TWY F 0.003 0.001  0.005 0.001  0.005 0.009  0.003 0.009
; bar across B, south of the runway
STOPBAR SB-B 0.003 0.0049  0.003 0.0051
)";

static TaxiDatabase MakeDb() {
    TaxiDatabase db;
    std::vector<std::string> errors;
    db.LoadText(kConfig, errors);
    CHECK(errors.empty());
    db.AddRunway("TEST", Runway{"09", "27", {0.004, 0.0}, {0.004, 0.01}});
    return db;
}

static void TestNormalize() {
    CHECK(NormalizeRoute(" a.b,  c  27l ") == "A B C 27L");
    CHECK(Tokenize("a b").size() == 2);
    CHECK(NormalizeRoute("") == "");
}

static void TestCoords() {
    double v;
    CHECK(ParseEsCoord("N023.25.40.500", v) && Near(v, 23 + 25 / 60.0 + 40.5 / 3600.0));
    CHECK(ParseEsCoord("W046.28.30.000", v) && Near(v, -(46 + 28 / 60.0 + 30 / 3600.0)));
    CHECK(!ParseEsCoord("X046.28.30.000", v));
    CHECK(!ParseEsCoord("N046.61.30.000", v));
    CHECK(FormatEsCoord(-(23 + 25 / 60.0 + 40.5 / 3600.0), true) == "S023.25.40.500");
    CHECK(FormatEsCoord(46.5, false) == "E046.30.00.000");

    std::vector<std::string> t = {"S023.25.40.000", "W046.28.30.000", "-23.5", "-46.4"};
    size_t i = 0;
    LatLon p;
    CHECK(ParseCoordPair(t, i, p) && i == 2 && p.lat < 0 && p.lon < 0);
    CHECK(ParseCoordPair(t, i, p) && i == 4 && Near(p.lat, -23.5) && Near(p.lon, -46.4));
}

static void TestConfigErrors() {
    TaxiDatabase db;
    std::vector<std::string> errors;
    db.LoadText("TWY A 0 0 0 1\nAIRPORT X\nTWY A 0 0\nFOO\nPOINT P 1 1 2 2\nTWY Z 0 0 bad 1\n", errors);
    CHECK(errors.size() == 5);
    CHECK(db.Find("x") != nullptr);
}

static void TestRouteToRunway() {
    TaxiDatabase db = MakeDb();
    const Airport* apt = db.FindNearest({0, 0.001}, 5);
    CHECK(apt && apt->icao == "TEST");
    PathResult r = BuildPath(*apt, "A B 27", {0, 0.001}, 90, false);  // bars: TestStopBars
    CHECK(r.unresolved.empty());
    CHECK(r.points.size() == 3);  // start(on A), junction A/B, hold short on B
    CHECK(Near(r.points[1].lat, 0) && Near(r.points[1].lon, 0.005));
    CHECK(Near(r.points.back().lon, 0.005));
    CHECK(Near(r.points.back().lat, 0.004 - 70.0 / 110540.0, 1e-6));
}

static void TestRouteNoTarget() {
    TaxiDatabase db = MakeDb();
    const Airport* apt = db.Find("TEST");
    // East on A then C: ends at the far end of C.
    PathResult r = BuildPath(*apt, "a c", {0.0002, 0.001}, 90);
    CHECK(Near(r.points.back().lat, 0.005) && Near(r.points.back().lon, 0.009));
    CHECK(Near(r.points[1].lat, 0) && Near(r.points[1].lon, 0.001));  // entry on A

    // Single taxiway, heading west: goes to the west end.
    r = BuildPath(*apt, "A", {0, 0.005}, 270);
    CHECK(Near(r.points.back().lon, 0.0));
    r = BuildPath(*apt, "A", {0, 0.005}, 90);
    CHECK(Near(r.points.back().lon, 0.01));
}

static void TestRouteToPointAndUnknown() {
    TaxiDatabase db = MakeDb();
    const Airport* apt = db.Find("TEST");
    PathResult r = BuildPath(*apt, "A C XYZ HP", {0, 0.001}, -1);
    CHECK(r.unresolved.size() == 1 && r.unresolved[0] == "XYZ");
    CHECK(Near(r.points.back().lat, 0.003) && Near(r.points.back().lon, 0.009));

    r = BuildPath(*apt, "QQQ", {0, 0.001}, -1);
    CHECK(r.points.empty());
}

static void TestRunwayCrossing() {
    TaxiDatabase db = MakeDb();
    const Airport* apt = db.Find("TEST");
    // Runway in the middle is a crossing and doesn't stop the route.
    PathResult r = BuildPath(*apt, "B 09 A", {-0.005, 0.005}, 0);
    CHECK(r.points.size() >= 2);
    CHECK(Near(r.points[1].lat, 0) && Near(r.points[1].lon, 0.005));
    CHECK(apt->HasRunway("RWY9") && apt->HasRunway("27") && !apt->HasRunway("18"));
}

static void TestRunwayEnd() {
    TaxiDatabase db = MakeDb();
    const Airport* apt = db.Find("TEST");
    // Parallel taxiway: stop next to the threshold of the runway asked for.
    PathResult r = BuildPath(*apt, "D 09", {0.003, 0.008}, 90);
    CHECK(Near(r.points.back().lon, 0.0));
    r = BuildPath(*apt, "D 27", {0.003, 0.002}, 270);
    CHECK(Near(r.points.back().lon, 0.01));
    r = BuildPath(*apt, "D 9", {0.003, 0.002}, 90);
    CHECK(Near(r.points.back().lon, 0.0));

    // Taxiway crossing the runway twice: hold short at the crossing near the 27 end.
    r = BuildPath(*apt, "F 27", {0.003, 0.001}, 0);
    CHECK(Near(r.points.back().lon, 0.009));
    CHECK(Near(r.points.back().lat, 0.004 + 70.0 / 110540.0, 1e-6));
    r = BuildPath(*apt, "F 09", {0.003, 0.001}, 0);
    CHECK(Near(r.points.back().lon, 0.001));
    CHECK(Near(r.points.back().lat, 0.004 - 70.0 / 110540.0, 1e-6));
}

static void TestStopBars() {
    TaxiDatabase db = MakeDb();
    const Airport* apt = db.Find("TEST");
    CHECK(apt->stopbars.count("SB-B") == 1);

    // Route A B 27 crosses SB-B before the runway hold: stops at the bar.
    PathResult r = BuildPath(*apt, "A B 27", {0, 0.001}, 90);
    CHECK(r.stopBar == "SB-B");
    CHECK(r.stopBarLine.size() == 2);
    CHECK(Near(r.points.back().lat, 0.003) && Near(r.points.back().lon, 0.005));

    // Naming the bar in the middle clears it, and the name is not reported as unknown.
    r = BuildPath(*apt, "A B SB-B 27", {0, 0.001}, 90);
    CHECK(r.stopBar.empty() && r.unresolved.empty());
    CHECK(Near(r.points.back().lat, 0.004 - 70.0 / 110540.0, 1e-6));

    // Naming the bar at the end means hold there, even with bars disabled
    // and when the taxiway would otherwise continue past it.
    r = BuildPath(*apt, "A B SB-B", {0, 0.001}, 90);
    CHECK(r.unresolved.empty());
    CHECK(Near(r.points.back().lat, 0.003) && Near(r.points.back().lon, 0.005));
    CHECK(r.stopBar == "SB-B");
    r = BuildPath(*apt, "A B SB-B", {0, 0.001}, 90, false);
    CHECK(Near(r.points.back().lat, 0.003) && Near(r.points.back().lon, 0.005));

    // Disabled, or aircraft sitting on the bar: not cut.
    r = BuildPath(*apt, "A B 27", {0, 0.001}, 90, false);
    CHECK(r.stopBar.empty());
    r = BuildPath(*apt, "B 27", {0.003, 0.005}, 0);
    CHECK(r.stopBar.empty());
    CHECK(Near(r.points.back().lat, 0.004 - 70.0 / 110540.0, 1e-6));
}

static void TestRouteEditing() {
    TaxiDatabase db = MakeDb();
    const Airport* apt = db.Find("TEST");

    CHECK(WithDefaultRunway(*apt, "a b", "27") == "A B 27");
    CHECK(WithDefaultRunway(*apt, "A B HS", "27") == "A B HS 27");  // unknown tokens skipped
    CHECK(WithDefaultRunway(*apt, "A B 09", "27") == "A B 09");
    CHECK(WithDefaultRunway(*apt, "A B SB-B", "27") == "A B SB-B");
    CHECK(WithDefaultRunway(*apt, "A C HP", "27") == "A C HP");
    CHECK(WithDefaultRunway(*apt, "A B", "36") == "A B");  // not a runway here
    CHECK(WithDefaultRunway(*apt, "A B", "") == "A B");

    // A B 27 stops at SB-B; releasing puts the bar before the runway.
    CHECK(ReleaseStopBar(*apt, "A B 27", "SB-B") == "A B SB-B 27");
    CHECK(BuildPath(*apt, ReleaseStopBar(*apt, "A B 27", "SB-B"), {0, 0.001}, 90).stopBar.empty());
    CHECK(ReleaseStopBar(*apt, "A B SB-B 27", "SB-B") == "A B SB-B 27");  // already released
    CHECK(HasReleasedStopBars(*apt, "A B SB-B 27"));
    CHECK(!HasReleasedStopBars(*apt, "A B SB-B"));  // trailing bar = hold there
    CHECK(LockStopBars(*apt, "A B SB-B 27") == "A B 27");
    CHECK(LockStopBars(*apt, "A B SB-B") == "A B SB-B");
}

int main() {
    TestNormalize();
    TestCoords();
    TestConfigErrors();
    TestRouteToRunway();
    TestRouteNoTarget();
    TestRouteToPointAndUnknown();
    TestRunwayCrossing();
    TestRunwayEnd();
    TestStopBars();
    TestRouteEditing();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("All tests passed\n");
    return 0;
}
