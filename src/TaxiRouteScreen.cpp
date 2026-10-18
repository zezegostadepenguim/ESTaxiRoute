#include "TaxiRouteScreen.h"

#include <string>
#include <vector>

#include "TaxiRoutePlugin.h"

using namespace EuroScopePlugIn;

namespace {

const double kMaxDrawSpeed = 50.0;  // don't draw for aircraft faster than this (kt)
const COLORREF kStopBarColor = RGB(255, 40, 40);

CPosition ToPosition(const taxi::LatLon& ll) {
    CPosition p;
    p.m_Latitude = ll.lat;
    p.m_Longitude = ll.lon;
    return p;
}

}  // namespace

CTaxiRouteScreen::CTaxiRouteScreen(CTaxiRoutePlugin* plugin) : plugin_(plugin) { plugin_->ScreenCreated(); }

void CTaxiRouteScreen::OnAsrContentToBeClosed() {
    plugin_->ScreenClosed();
    delete this;
}

void CTaxiRouteScreen::OnRefresh(HDC hDC, int Phase) {
    if (Phase != REFRESH_PHASE_BEFORE_TAGS) return;
    plugin_->ScreenRefreshed();
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

    HPEN thin = CreatePen(PS_SOLID, 2, plugin_->Color());
    HPEN thick = CreatePen(PS_SOLID, 3, plugin_->Color());
    HPEN bar = CreatePen(PS_SOLID, 4, kStopBarColor);
    HGDIOBJ oldPen = SelectObject(hDC, thin);
    SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
    SetTextColor(hDC, plugin_->Color());

    for (CRadarTarget rt = plugin_->RadarTargetSelectFirst(); rt.IsValid();
         rt = plugin_->RadarTargetSelectNext(rt)) {
        std::string cs = rt.GetCallsign();
        if (mode == ShowMode::Asel && cs != asel) continue;
        if (plugin_->IsHidden(cs)) continue;

        if (rt.GetPosition().GetReportedGS() > kMaxDrawSpeed) continue;
        taxi::PathResult path;
        if (!plugin_->ComputePath(rt, rt.GetCorrelatedFlightPlan(), path)) continue;
        if (path.points.size() < 2) continue;

        std::vector<POINT> px;
        for (const taxi::LatLon& ll : path.points) px.push_back(ConvertCoordFromPositionToPixel(ToPosition(ll)));
        SelectObject(hDC, cs == asel ? thick : thin);
        Polyline(hDC, px.data(), static_cast<int>(px.size()));

        POINT end = px.back();
        Ellipse(hDC, end.x - 4, end.y - 4, end.x + 5, end.y + 5);
        TextOutA(hDC, end.x + 7, end.y - 7, cs.c_str(), static_cast<int>(cs.size()));

        if (!path.stopBar.empty()) {
            // Route held at a stop bar: show the bar in red.
            std::vector<POINT> bp;
            for (const taxi::LatLon& ll : path.stopBarLine) bp.push_back(ConvertCoordFromPositionToPixel(ToPosition(ll)));
            SelectObject(hDC, bar);
            Polyline(hDC, bp.data(), static_cast<int>(bp.size()));
        }
    }

    SelectObject(hDC, oldPen);
    DeleteObject(thin);
    DeleteObject(thick);
    DeleteObject(bar);
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

    HPEN barPen = CreatePen(PS_SOLID, 2, kStopBarColor);
    SelectObject(hDC, barPen);
    SetTextColor(hDC, kStopBarColor);
    for (const auto& a : plugin_->Database().Airports()) {
        for (const auto& sb : a.second.stopbars) {
            for (const auto& line : sb.second) {
                std::vector<POINT> px;
                for (const taxi::LatLon& ll : line) px.push_back(ConvertCoordFromPositionToPixel(ToPosition(ll)));
                Polyline(hDC, px.data(), static_cast<int>(px.size()));
                POINT e = px.back();
                TextOutA(hDC, e.x + 3, e.y + 3, sb.first.c_str(), static_cast<int>(sb.first.size()));
            }
        }
    }

    SelectObject(hDC, oldPen);
    DeleteObject(pen);
    DeleteObject(barPen);
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
