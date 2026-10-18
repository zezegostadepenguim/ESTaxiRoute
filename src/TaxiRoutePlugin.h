#pragma once

#include <windows.h>

#include <map>
#include <string>
#include <vector>

#include "EuroScopePlugIn.h"
#include "TaxiCore.h"

#define PLUGIN_NAME "ESTaxiRoute"
#define PLUGIN_VERSION "1.0.0"
#define PLUGIN_AUTHOR "ESTaxiRoute contributors"
#define PLUGIN_COPYRIGHT "MIT License"

// Tag items
const int TAG_ITEM_TAXI_ROUTE = 1;
const int TAG_ITEM_TAXI_FLAG = 2;
// Tag functions
const int TAG_FUNC_TAXI_EDIT = 1;
const int TAG_FUNC_TAXI_CLEAR = 2;
const int TAG_FUNC_TAXI_TOGGLE_DRAW = 3;
const int TAG_FUNC_TAXI_EDIT_DONE = 100;  // popup callback, not registered

enum class ShowMode { Off, Asel, All };

// Points being clicked on the ASR to create a taxiway/point in the config file.
struct Recording {
    bool active = false;
    std::string kind;  // "TWY", "POINT" or "STOPBAR" (config file keyword)
    std::string icao;
    std::string name;
    std::vector<taxi::LatLon> points;
};

class CTaxiRoutePlugin : public EuroScopePlugIn::CPlugIn {
public:
    CTaxiRoutePlugin();

    void OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget,
                      int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB,
                      double* pFontSize) override;
    void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area) override;
    EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent,
                                                        bool GeoReferenced, bool CanBeSaved,
                                                        bool CanBeCreated) override;
    bool OnCompileCommand(const char* sCommandLine) override;
    void OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan) override;
    void OnRadarTargetPositionUpdate(EuroScopePlugIn::CRadarTarget RadarTarget) override;
    void OnAirportRunwayActivityChanged() override;

    // Route storage
    std::string GetRoute(EuroScopePlugIn::CFlightPlan fp);
    std::string GetRoute(const std::string& callsign);
    void SetRoute(EuroScopePlugIn::CFlightPlan fp, const std::string& route);
    bool IsHidden(const std::string& callsign) const { return hidden_.count(callsign) > 0; }

    // Data used by the radar screens
    taxi::TaxiDatabase& Database();
    ShowMode Show() const { return show_; }
    bool Debug() const { return debug_; }
    bool StopBars() const { return stopBars_; }
    COLORREF Color() const { return color_; }
    Recording& Rec() { return rec_; }
    void RecordClick(const taxi::LatLon& pos);

private:
    void LoadSettings();
    void SaveSettings();
    void LoadDatabase(bool verbose);
    void LoadRunways();
    void Message(const std::string& msg);
    bool FinishRecording();
    std::string ConfigPath() const;

    std::map<std::string, std::string> routes_;  // callsign -> route
    std::map<std::string, bool> hidden_;         // callsigns with drawing toggled off
    taxi::TaxiDatabase db_;
    bool runwaysLoaded_ = false;
    ShowMode show_ = ShowMode::All;
    bool debug_ = false;
    bool stopBars_ = true;  // routes stop at stop bars they don't name
    int annotationIndex_ = -1;  // flight strip annotation used to share routes, -1 = off
    double clearSpeed_ = 80.0;  // routes are cleared when the aircraft goes faster (takeoff)
    COLORREF color_ = RGB(255, 200, 0);
    Recording rec_;
};
