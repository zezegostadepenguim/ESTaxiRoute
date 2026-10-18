#include "TaxiRoutePlugin.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "TaxiRouteScreen.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

using namespace EuroScopePlugIn;

namespace {

const char* kAnnotationPrefix = "TX:";

std::string Upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string tok;
    while (in >> tok) out.push_back(tok);
    return out;
}

const char* kHelp[] = {
    ".taxi <rota>              - define a rota do trafego selecionado (ASEL), ex: .taxi A B C 27L",
    ".taxi clear               - apaga a rota do ASEL",
    ".taxi show all|asel|off   - desenha todas as rotas / so a do ASEL / nenhuma",
    ".taxi debug               - mostra/oculta as taxiways carregadas do arquivo",
    ".taxi reload              - recarrega ESTaxiRoute.txt",
    ".taxi annot <0-8>|off     - compartilha a rota numa anotacao da strip",
    ".taxi vsmr on|off         - mostra a rota no item uk_stand do vSMR (anotacao 3)",
    ".taxi autorwy on|off      - completa a rota com a pista do plano de voo",
    ".taxi release / lock      - libera a proxima barra de parada do ASEL / trava todas",
    ".taxi color <R> <G> <B>   - cor do desenho",
    ".taxi bars on|off         - rotas param nas barras de parada (STOPBAR) nao liberadas",
    ".taxi rec <ICAO> <TWY>    - grava uma taxiway clicando no radar (.taxi save / undo / cancel)",
    ".taxi recpt <ICAO> <NOME> - grava um ponto (holding point, stand) com um clique",
    ".taxi recbar <ICAO> <NOME> - grava uma barra de parada com dois cliques (um de cada lado)",
};

}  // namespace

CTaxiRoutePlugin::CTaxiRoutePlugin()
    : CPlugIn(COMPATIBILITY_CODE, PLUGIN_NAME, PLUGIN_VERSION, PLUGIN_AUTHOR, PLUGIN_COPYRIGHT) {
    RegisterTagItemType("Taxi route", TAG_ITEM_TAXI_ROUTE);
    RegisterTagItemType("Taxi route flag (TX)", TAG_ITEM_TAXI_FLAG);
    RegisterTagItemFunction("Edit taxi route", TAG_FUNC_TAXI_EDIT);
    RegisterTagItemFunction("Clear taxi route", TAG_FUNC_TAXI_CLEAR);
    RegisterTagItemFunction("Toggle taxi route drawing", TAG_FUNC_TAXI_TOGGLE_DRAW);
    RegisterTagItemFunction("Release/lock stop bar", TAG_FUNC_TAXI_STOPBAR);

    LoadSettings();
    LoadDatabase(true);
    Message(std::string("versao ") + PLUGIN_VERSION + " carregada. Digite .taxi help para os comandos.");
}

// ---- Settings / data -------------------------------------------------------

std::string CTaxiRoutePlugin::ConfigPath() const {
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(reinterpret_cast<HINSTANCE>(&__ImageBase), path, MAX_PATH);
    std::string dir(path);
    size_t slash = dir.find_last_of("\\/");
    dir = slash == std::string::npos ? "" : dir.substr(0, slash + 1);
    return dir + "ESTaxiRoute.txt";
}

void CTaxiRoutePlugin::LoadSettings() {
    if (const char* v = GetDataFromSettings("show")) {
        std::string s = Upper(v);
        show_ = s == "OFF" ? ShowMode::Off : s == "ASEL" ? ShowMode::Asel : ShowMode::All;
    }
    if (const char* v = GetDataFromSettings("annot")) {
        int i = std::atoi(v);
        annotationIndex_ = (*v && i >= 0 && i <= 8) ? i : -1;
    }
    if (const char* v = GetDataFromSettings("color")) {
        int r, g, b;
        if (std::sscanf(v, "%d,%d,%d", &r, &g, &b) == 3) color_ = RGB(r, g, b);
    }
    if (const char* v = GetDataFromSettings("bars")) stopBars_ = Upper(v) != "OFF";
    if (const char* v = GetDataFromSettings("annotraw")) annotationRaw_ = Upper(v) == "ON";
    if (const char* v = GetDataFromSettings("autorwy")) autoRunway_ = Upper(v) != "OFF";
    if (const char* v = GetDataFromSettings("clearspeed")) {
        double s = std::atof(v);
        if (s > 0) clearSpeed_ = s;
    }
}

void CTaxiRoutePlugin::SaveSettings() {
    const char* show = show_ == ShowMode::Off ? "OFF" : show_ == ShowMode::Asel ? "ASEL" : "ALL";
    SaveDataToSettings("show", "ESTaxiRoute: rotas desenhadas", show);
    std::string annot = annotationIndex_ >= 0 ? std::to_string(annotationIndex_) : "";
    SaveDataToSettings("annot", "ESTaxiRoute: anotacao da strip", annot.c_str());
    char color[32];
    std::snprintf(color, sizeof(color), "%d,%d,%d", GetRValue(color_), GetGValue(color_), GetBValue(color_));
    SaveDataToSettings("color", "ESTaxiRoute: cor", color);
    SaveDataToSettings("bars", "ESTaxiRoute: barras de parada", stopBars_ ? "ON" : "OFF");
    SaveDataToSettings("annotraw", "ESTaxiRoute: anotacao sem prefixo (vSMR)", annotationRaw_ ? "ON" : "OFF");
    SaveDataToSettings("autorwy", "ESTaxiRoute: pista do plano de voo", autoRunway_ ? "ON" : "OFF");
}

void CTaxiRoutePlugin::LoadDatabase(bool verbose) {
    std::vector<std::string> errors;
    std::string path = ConfigPath();
    if (!db_.LoadFile(path, errors)) {
        if (verbose)
            Message("arquivo " + path + " nao encontrado. As rotas funcionam no tag, mas nao serao "
                    "desenhadas na ASR ate voce cadastrar as taxiways (.taxi rec).");
    } else {
        size_t shown = 0;
        for (const std::string& e : errors) {
            if (shown++ == 5) {
                Message("... mais " + std::to_string(errors.size() - 5) + " erro(s)");
                break;
            }
            Message("ESTaxiRoute.txt " + e);
        }
        if (verbose) {
            size_t twys = 0;
            for (const auto& a : db_.Airports()) twys += a.second.taxiways.size();
            size_t bars = 0;
            for (const auto& a : db_.Airports()) bars += a.second.stopbars.size();
            Message(std::to_string(db_.Airports().size()) + " aeroporto(s), " + std::to_string(twys) +
                    " taxiway(s), " + std::to_string(bars) + " barra(s) de parada carregadas.");
        }
    }
    runwaysLoaded_ = false;
}

void CTaxiRoutePlugin::LoadRunways() {
    db_.ClearRunways();
    int count = 0;
    for (CSectorElement e = SectorFileElementSelectFirst(SECTOR_ELEMENT_RUNWAY); e.IsValid();
         e = SectorFileElementSelectNext(e, SECTOR_ELEMENT_RUNWAY)) {
        CPosition p1, p2;
        if (!e.GetPosition(&p1, 0) || !e.GetPosition(&p2, 1)) continue;
        std::vector<std::string> apt = Split(e.GetAirportName() ? e.GetAirportName() : "");
        if (apt.empty()) continue;
        taxi::Runway r;
        r.name1 = e.GetRunwayName(0);
        r.name2 = e.GetRunwayName(1);
        r.thr1 = {p1.m_Latitude, p1.m_Longitude};
        r.thr2 = {p2.m_Latitude, p2.m_Longitude};
        db_.AddRunway(apt[0], r);
        ++count;
    }
    runwaysLoaded_ = count > 0;  // sector file may not be loaded yet; retry later
}

taxi::TaxiDatabase& CTaxiRoutePlugin::Database() {
    if (!runwaysLoaded_) LoadRunways();
    return db_;
}

void CTaxiRoutePlugin::OnAirportRunwayActivityChanged() { runwaysLoaded_ = false; }

void CTaxiRoutePlugin::Message(const std::string& msg) {
    DisplayUserMessage(PLUGIN_NAME, "", msg.c_str(), true, true, false, false, false);
}

// ---- Route storage ---------------------------------------------------------

std::string CTaxiRoutePlugin::GetRoute(const std::string& callsign) {
    auto it = routes_.find(callsign);
    return it == routes_.end() ? "" : it->second;
}

std::string CTaxiRoutePlugin::GetRoute(CFlightPlan fp) {
    if (!fp.IsValid()) return "";
    std::string cs = fp.GetCallsign();
    if (annotationIndex_ >= 0) {
        // A route shared by another controller through the strip annotation.
        const char* a = fp.GetControllerAssignedData().GetFlightStripAnnotation(annotationIndex_);
        if (annotationRaw_ && a && *a) {
            routes_[cs] = a;
            return routes_[cs];
        }
        if (!annotationRaw_ && a && std::string(a).compare(0, 3, kAnnotationPrefix) == 0 && a[3]) {
            routes_[cs] = a + 3;
            return routes_[cs];
        }
    }
    return GetRoute(cs);
}

bool CTaxiRoutePlugin::WriteAnnotation(CFlightPlan fp, const std::string& route) {
    if (annotationIndex_ < 0) return true;
    CFlightPlanControllerAssignedData data = fp.GetControllerAssignedData();
    const char* cur = data.GetFlightStripAnnotation(annotationIndex_);
    // Never overwrite an annotation that isn't ours. In raw (vSMR) mode the
    // annotation is dedicated to the route.
    bool ours = annotationRaw_ || !cur || !*cur || std::string(cur).compare(0, 3, kAnnotationPrefix) == 0;
    if (!ours) return true;
    std::string value = route.empty() || annotationRaw_ ? route : kAnnotationPrefix + route;
    if (cur && value == cur) return true;
    return data.SetFlightStripAnnotation(annotationIndex_, value.c_str());
}

void CTaxiRoutePlugin::SyncAnnotations() {
    // Routes set before the annotation was turned on are only stored locally.
    int written = 0, failed = 0;
    std::map<std::string, std::string> routes = routes_;
    for (const auto& r : routes) {
        CFlightPlan fp = FlightPlanSelect(r.first.c_str());
        if (!fp.IsValid()) continue;
        if (WriteAnnotation(fp, r.second))
            ++written;
        else
            ++failed;
    }
    if (written || failed)
        Message(std::to_string(written) + " rota(s) gravada(s) na anotacao" +
                (failed ? ", " + std::to_string(failed) + " recusada(s) (trafego trackeado por outro controlador?)"
                        : std::string()));
}

void CTaxiRoutePlugin::SetRoute(CFlightPlan fp, const std::string& route) {
    if (!fp.IsValid()) return;
    std::string cs = fp.GetCallsign();
    std::string norm = taxi::NormalizeRoute(route);
    if (norm.empty())
        routes_.erase(cs);
    else
        routes_[cs] = norm;

    if (!WriteAnnotation(fp, norm))
        Message(cs + ": o EuroScope nao aceitou gravar a rota na anotacao " + std::to_string(annotationIndex_) +
                " (voce precisa estar trackeando o trafego ou ele estar sem controle). A rota ficou so local.");

    if (!norm.empty()) {
        const taxi::Airport* apt = nullptr;
        CRadarTarget rt = fp.GetCorrelatedRadarTarget();
        if (rt.IsValid()) {
            CPosition p = rt.GetPosition().GetPosition();
            apt = Database().FindNearest({p.m_Latitude, p.m_Longitude}, 10);
        }
        if (apt) {
            taxi::PathResult r = taxi::BuildPath(*apt, norm, {0, 0}, -1);
            if (!r.unresolved.empty()) {
                std::string list;
                for (const std::string& t : r.unresolved) list += " " + t;
                Message(cs + ": nao encontrado em " + apt->icao + ":" + list);
            }
        }
    }
}

bool CTaxiRoutePlugin::ComputePath(CRadarTarget rt, CFlightPlan fp, taxi::PathResult& path,
                                   const taxi::Airport** airport) {
    if (!rt.IsValid()) return false;
    std::string route = fp.IsValid() ? GetRoute(fp) : GetRoute(rt.GetCallsign());
    if (route.empty()) return false;
    CRadarTargetPositionData pd = rt.GetPosition();
    if (!pd.IsValid()) return false;
    CPosition pos = pd.GetPosition();
    taxi::LatLon start{pos.m_Latitude, pos.m_Longitude};
    const taxi::Airport* apt = Database().FindNearest(start, 10);
    if (!apt) return false;

    if (autoRunway_ && fp.IsValid() && Upper(fp.GetFlightPlanData().GetOrigin()) == apt->icao) {
        const char* rwy = fp.GetFlightPlanData().GetDepartureRwy();
        route = taxi::WithDefaultRunway(*apt, route, rwy ? rwy : "");
    }
    path = taxi::BuildPath(*apt, route, start, pd.GetReportedHeading(), stopBars_);
    if (airport) *airport = apt;
    return true;
}

void CTaxiRoutePlugin::ToggleStopBar(CFlightPlan fp, bool release, bool lock) {
    taxi::PathResult path;
    const taxi::Airport* apt = nullptr;
    if (!ComputePath(fp.GetCorrelatedRadarTarget(), fp, path, &apt)) return;
    std::string route = GetRoute(fp);
    std::string cs = fp.GetCallsign();

    // Release the bar the route is held at, unless it is the bar the route
    // was told to hold at (the last element).
    std::vector<std::string> tok = taxi::Tokenize(route);
    bool heldByRoute = !tok.empty() && tok.back() == path.stopBar;
    if (release && !path.stopBar.empty() && !heldByRoute) {
        SetRoute(fp, taxi::ReleaseStopBar(*apt, route, path.stopBar));
        Message(cs + ": " + path.stopBar + " liberada");
    } else if (lock && taxi::HasReleasedStopBars(*apt, route)) {
        SetRoute(fp, taxi::LockStopBars(*apt, route));
        Message(cs + ": barras travadas");
    } else if (release && heldByRoute) {
        Message(cs + ": " + path.stopBar + " e o limite da rota; edite a rota para seguir alem dela");
    }
}

// ---- EuroScope callbacks ---------------------------------------------------

void CTaxiRoutePlugin::OnGetTagItem(CFlightPlan FlightPlan, CRadarTarget RadarTarget, int ItemCode,
                                    int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB,
                                    double* pFontSize) {
    std::string route = FlightPlan.IsValid() ? GetRoute(FlightPlan)
                        : RadarTarget.IsValid() ? GetRoute(RadarTarget.GetCallsign())
                                                : "";
    if (ItemCode == TAG_ITEM_TAXI_ROUTE) {
        if (route.size() > 15) route = route.substr(0, 14) + "+";
        std::snprintf(sItemString, 16, "%s", route.c_str());
    } else if (ItemCode == TAG_ITEM_TAXI_FLAG) {
        std::snprintf(sItemString, 16, "%s", route.empty() ? "" : "TX");
    }
}

void CTaxiRoutePlugin::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area) {
    CFlightPlan fp = FlightPlanSelectASEL();
    if (!fp.IsValid()) return;
    switch (FunctionId) {
        case TAG_FUNC_TAXI_EDIT:
            OpenPopupEdit(Area, TAG_FUNC_TAXI_EDIT_DONE, GetRoute(fp).c_str());
            break;
        case TAG_FUNC_TAXI_EDIT_DONE:
            SetRoute(fp, sItemString ? sItemString : "");
            break;
        case TAG_FUNC_TAXI_CLEAR:
            SetRoute(fp, "");
            break;
        case TAG_FUNC_TAXI_STOPBAR:
            ToggleStopBar(fp, true, true);
            break;
        case TAG_FUNC_TAXI_TOGGLE_DRAW: {
            std::string cs = fp.GetCallsign();
            if (hidden_.count(cs))
                hidden_.erase(cs);
            else
                hidden_[cs] = true;
            break;
        }
    }
}

CRadarScreen* CTaxiRoutePlugin::OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent,
                                                     bool GeoReferenced, bool CanBeSaved,
                                                     bool CanBeCreated) {
    return new CTaxiRouteScreen(this);
}

void CTaxiRoutePlugin::OnFlightPlanDisconnect(CFlightPlan FlightPlan) {
    routes_.erase(FlightPlan.GetCallsign());
    hidden_.erase(FlightPlan.GetCallsign());
}

void CTaxiRoutePlugin::OnRadarTargetPositionUpdate(CRadarTarget RadarTarget) {
    if (RadarTarget.GetPosition().GetReportedGS() < clearSpeed_) return;
    std::string cs = RadarTarget.GetCallsign();
    if (!routes_.count(cs)) return;
    // Took off: the taxi route is no longer relevant.
    CFlightPlan fp = RadarTarget.GetCorrelatedFlightPlan();
    if (fp.IsValid())
        SetRoute(fp, "");
    else
        routes_.erase(cs);
}

bool CTaxiRoutePlugin::OnCompileCommand(const char* sCommandLine) {
    std::vector<std::string> args = Split(sCommandLine);
    if (args.empty() || Upper(args[0]) != ".TAXI") return false;

    std::string cmd = args.size() > 1 ? Upper(args[1]) : "HELP";
    if (cmd == "HELP") {
        for (const char* line : kHelp) Message(line);
    } else if (cmd == "CLEAR") {
        CFlightPlan fp = FlightPlanSelectASEL();
        if (fp.IsValid()) SetRoute(fp, "");
    } else if (cmd == "SHOW" && args.size() > 2) {
        std::string m = Upper(args[2]);
        show_ = m == "OFF" ? ShowMode::Off : m == "ASEL" ? ShowMode::Asel : ShowMode::All;
        SaveSettings();
    } else if (cmd == "DEBUG") {
        debug_ = !debug_;
    } else if (cmd == "RELOAD") {
        LoadDatabase(true);
    } else if (cmd == "ANNOT" && args.size() > 2) {
        int i = std::atoi(args[2].c_str());
        bool off = Upper(args[2]) == "OFF" || i < 0 || i > 8 || !std::isdigit(static_cast<unsigned char>(args[2][0]));
        annotationIndex_ = off ? -1 : i;
        annotationRaw_ = false;
        SaveSettings();
        SyncAnnotations();
        Message(off ? "compartilhamento por anotacao desligado"
                    : "rotas compartilhadas na anotacao " + std::to_string(i));
    } else if (cmd == "COLOR" && args.size() > 4) {
        color_ = RGB(std::atoi(args[2].c_str()), std::atoi(args[3].c_str()), std::atoi(args[4].c_str()));
        SaveSettings();
    } else if (cmd == "VSMR" && args.size() > 2) {
        bool on = Upper(args[2]) != "OFF";
        annotationIndex_ = on ? 3 : -1;
        annotationRaw_ = on;
        SaveSettings();
        Message(on ? "rotas gravadas na anotacao 3: use o item uk_stand no tag do vSMR"
                   : "integracao com o vSMR desligada");
        if (on) SyncAnnotations();
    } else if (cmd == "AUTORWY" && args.size() > 2) {
        autoRunway_ = Upper(args[2]) != "OFF";
        SaveSettings();
        Message(autoRunway_ ? "rotas terminadas em taxiway vao ate a pista do plano de voo"
                            : "pista do plano de voo nao e usada");
    } else if (cmd == "RELEASE" || cmd == "LOCK") {
        CFlightPlan fp = FlightPlanSelectASEL();
        if (fp.IsValid()) ToggleStopBar(fp, cmd == "RELEASE", cmd == "LOCK");
    } else if (cmd == "BARS" && args.size() > 2) {
        stopBars_ = Upper(args[2]) != "OFF";
        SaveSettings();
        Message(stopBars_ ? "rotas param nas barras de parada" : "barras de parada ignoradas");
    } else if ((cmd == "REC" || cmd == "RECPT" || cmd == "RECBAR") && args.size() > 3) {
        rec_ = Recording();
        rec_.active = true;
        rec_.kind = cmd == "RECPT" ? "POINT" : cmd == "RECBAR" ? "STOPBAR" : "TWY";
        rec_.icao = Upper(args[2]);
        rec_.name = Upper(args[3]);
        if (rec_.kind == "POINT")
            Message("clique no radar na posicao do ponto " + rec_.name);
        else if (rec_.kind == "STOPBAR")
            Message("clique dos dois lados da taxiway, atravessando-a, onde fica a barra " + rec_.name);
        else
            Message("clique no radar ao longo da linha central da taxiway " + rec_.name +
                    ". Use .taxi undo para desfazer, .taxi save para salvar, .taxi cancel para sair.");
    } else if (cmd == "UNDO") {
        if (!rec_.points.empty()) rec_.points.pop_back();
    } else if (cmd == "SAVE") {
        FinishRecording();
    } else if (cmd == "CANCEL") {
        rec_ = Recording();
    } else {
        // Anything else is a route for the selected aircraft.
        CFlightPlan fp = FlightPlanSelectASEL();
        if (!fp.IsValid()) {
            Message("nenhum trafego selecionado (ASEL)");
            return true;
        }
        std::string route;
        for (size_t i = 1; i < args.size(); ++i) route += args[i] + " ";
        SetRoute(fp, route);
    }
    return true;
}

// ---- Recording -------------------------------------------------------------

void CTaxiRoutePlugin::RecordClick(const taxi::LatLon& pos) {
    if (!rec_.active) return;
    rec_.points.push_back(pos);
    if ((rec_.kind == "POINT" && rec_.points.size() == 1) || (rec_.kind == "STOPBAR" && rec_.points.size() == 2))
        FinishRecording();
}

bool CTaxiRoutePlugin::FinishRecording() {
    if (!rec_.active) return false;
    size_t needed = rec_.kind == "POINT" ? 1 : 2;
    if (rec_.points.size() < needed) {
        Message("pontos insuficientes para salvar " + rec_.name);
        return false;
    }
    std::ofstream out(ConfigPath(), std::ios::app);
    if (!out) {
        Message("nao foi possivel escrever em " + ConfigPath());
        return false;
    }
    out << "\nAIRPORT " << rec_.icao << "\n" << rec_.kind << " " << rec_.name;
    for (const taxi::LatLon& p : rec_.points)
        out << " " << taxi::FormatEsCoord(p.lat, true) << " " << taxi::FormatEsCoord(p.lon, false);
    out << "\n";
    out.close();
    Message(rec_.name + " salvo em " + rec_.icao);
    rec_ = Recording();
    LoadDatabase(false);
    return true;
}
