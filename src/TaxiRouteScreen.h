#pragma once

#include <windows.h>

#include "EuroScopePlugIn.h"

class CTaxiRoutePlugin;

const int SCREEN_OBJECT_RECORD = 1;

class CTaxiRouteScreen : public EuroScopePlugIn::CRadarScreen {
public:
    explicit CTaxiRouteScreen(CTaxiRoutePlugin* plugin) : plugin_(plugin) {}

    void OnAsrContentToBeClosed() override { delete this; }
    void OnRefresh(HDC hDC, int Phase) override;
    void OnClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button) override;

private:
    void DrawRoutes(HDC hDC);
    void DrawDatabase(HDC hDC);
    void DrawRecording(HDC hDC);

    CTaxiRoutePlugin* plugin_;
};
