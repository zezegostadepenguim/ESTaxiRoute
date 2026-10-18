#include "TaxiRouteScreen.h"

#include <string>
#include <vector>

#include "TaxiRoutePlugin.h"

using namespace EuroScopePlugIn;

namespace {

const double kMaxDrawSpeed = 50.0;  // don't draw for aircraft faster than this (kt)

CPosition ToPosition(const taxi::LatLon& ll) {
    CPosition p;
    p.m_Latitude = ll.lat;
    p.m_Longitude = ll.lon;
    return p;
}

}  // namespace

void CTaxiRouteScreen::OnRefresh(HDC hDC, int Phase) {
    if (Phase != REFRESH_PHASE_BEFORE_TAGS) return;
    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    if (plugin_->Debug()) DrawDatabase(hDC);
    DrawRoutes(hDC);
    if (plugin_->Rec().active) {
        DrawRecording(hDC);
        AddScreenObject(SCREEN_OBJECT_RECORD, "REC", GetRadarArea(), false,
                        "ESTaxiRoute: clique para adicionar ponto");
    }
    RestoreDC(hDC, saved);
}

void CTaxiRouteScreen::OnClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area,
                                           int Button) {
    if (ObjectType != SCREEN_OBJECT_RECORD || Button != BUTTON_LEFT) return;
    CPosition p = ConvertCoordFromPixelToPosition(Pt);
    plugin_->RecordClick({p.m_Latitude, p.m_Longitude});
    RequestRefresh();
}

void CTaxiRouteScreen::DrawRoutes(HDC hDC) {
    ShowMode mode = plugin_->Show();
    if (mode == ShowMode::Off) return;

    std::string asel;
    CFlightPlan aselFp = plugin_->FlightPlanSelectASEL();
    if (aselFp.IsValid()) asel = aselFp.GetCallsign();

    taxi::TaxiDatabase& db = plugin_->Database();
    HPEN thin = CreatePen(PS_SOLID, 2, plugin_->Color());
    HPEN thick = CreatePen(PS_SOLID, 3, plugin_->Color());
    HGDIOBJ oldPen = SelectObject(hDC, thin);
    SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
    SetTextColor(hDC, plugin_->Color());

    for (CRadarTarget rt = plugin_->RadarTargetSelectFirst(); rt.IsValid();
         rt = plugin_->RadarTargetSelectNext(rt)) {
        std::string cs = rt.GetCallsign();
        if (mode == ShowMode::Asel && cs != asel) continue;
        if (plugin_->IsHidden(cs)) continue;

        CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        std::string route = fp.IsValid() ? plugin_->GetRoute(fp) : plugin_->GetRoute(cs);
        if (route.empty()) continue;

        CRadarTargetPositionData pd = rt.GetPosition();
        if (!pd.IsValid() || pd.GetReportedGS() > kMaxDrawSpeed) continue;
        CPosition pos = pd.GetPosition();
        taxi::LatLon start{pos.m_Latitude, pos.m_Longitude};
        const taxi::Airport* apt = db.FindNearest(start, 10);
        if (!apt) continue;

        taxi::PathResult path = taxi::BuildPath(*apt, route, start, pd.GetReportedHeading());
        if (path.points.size() < 2) continue;

        std::vector<POINT> px;
        for (const taxi::LatLon& ll : path.points) px.push_back(ConvertCoordFromPositionToPixel(ToPosition(ll)));
        SelectObject(hDC, cs == asel ? thick : thin);
        Polyline(hDC, px.data(), static_cast<int>(px.size()));

        POINT end = px.back();
        Ellipse(hDC, end.x - 4, end.y - 4, end.x + 5, end.y + 5);
        TextOutA(hDC, end.x + 7, end.y - 7, cs.c_str(), static_cast<int>(cs.size()));
    }

    SelectObject(hDC, oldPen);
    DeleteObject(thin);
    DeleteObject(thick);
}

void CTaxiRouteScreen::DrawDatabase(HDC hDC) {
    HPEN pen = CreatePen(PS_DOT, 1, RGB(0, 200, 255));
    HGDIOBJ oldPen = SelectObject(hDC, pen);
    SetTextColor(hDC, RGB(0, 200, 255));

    for (const auto& a : plugin_->Database().Airports()) {
        for (const auto& tw : a.second.taxiways) {
            for (const auto& line : tw.second) {
                std::vector<POINT> px;
                for (const taxi::LatLon& ll : line) px.push_back(ConvertCoordFromPositionToPixel(ToPosition(ll)));
                Polyline(hDC, px.data(), static_cast<int>(px.size()));
                POINT mid = px[px.size() / 2];
                TextOutA(hDC, mid.x + 3, mid.y + 3, tw.first.c_str(), static_cast<int>(tw.first.size()));
            }
        }
        for (const auto& p : a.second.points) {
            POINT c = ConvertCoordFromPositionToPixel(ToPosition(p.second));
            MoveToEx(hDC, c.x - 4, c.y, nullptr);
            LineTo(hDC, c.x + 5, c.y);
            MoveToEx(hDC, c.x, c.y - 4, nullptr);
            LineTo(hDC, c.x, c.y + 5);
            TextOutA(hDC, c.x + 5, c.y + 3, p.first.c_str(), static_cast<int>(p.first.size()));
        }
    }

    SelectObject(hDC, oldPen);
    DeleteObject(pen);
}

void CTaxiRouteScreen::DrawRecording(HDC hDC) {
    const Recording& rec = plugin_->Rec();
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 0, 255));
    HGDIOBJ oldPen = SelectObject(hDC, pen);
    SetTextColor(hDC, RGB(255, 0, 255));

    std::vector<POINT> px;
    for (const taxi::LatLon& ll : rec.points) px.push_back(ConvertCoordFromPositionToPixel(ToPosition(ll)));
    if (px.size() > 1) Polyline(hDC, px.data(), static_cast<int>(px.size()));
    for (const POINT& p : px) Rectangle(hDC, p.x - 2, p.y - 2, p.x + 3, p.y + 3);

    std::string label = "GRAVANDO " + rec.icao + " " + rec.name + " (" + std::to_string(rec.points.size()) + " pts)";
    RECT area = GetRadarArea();
    TextOutA(hDC, area.left + 10, area.top + 10, label.c_str(), static_cast<int>(label.size()));

    SelectObject(hDC, oldPen);
    DeleteObject(pen);
}
