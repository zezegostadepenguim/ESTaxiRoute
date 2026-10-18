#include "TaxiRoutePlugin.h"

static CTaxiRoutePlugin* gPlugin = nullptr;

void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance) {
    *ppPlugInInstance = gPlugin = new CTaxiRoutePlugin();
}

void __declspec(dllexport) EuroScopePlugInExit(void) {
    delete gPlugin;
    gPlugin = nullptr;
}
