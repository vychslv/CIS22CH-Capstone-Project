/**
 * Air Travel Database - Honors Capstone Project
 * C++ web application front-ending OpenFlights data (airports, airlines, routes).
 * Serves on port 8080. Uses Crow framework.
 */

#include <crow.h>
#define _USE_MATH_DEFINES
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <memory>

namespace {

const double EARTH_RADIUS_MILES = 3958.8;

// ----- Data structures -----
struct Airport {
    int id{0};
    std::string name;
    std::string city;
    std::string country;
    std::string iata;
    std::string icao;
    double lat{0}, lon{0};
    int altitude{0};
};

struct Airline {
    int id{0};
    std::string name;
    std::string alias;
    std::string iata;
    std::string icao;
    std::string country;
    std::string active;
};

struct Route {
    std::string airline_code;
    int airline_id{0};
    std::string src_airport;
    int src_airport_id{0};
    std::string dest_airport;
    int dest_airport_id{0};
    std::string codeshare;
    int stops{0};
    std::string equipment;
};

// ----- CSV parsing (handles quoted fields and \N) -----
std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::vector<std::string> parse_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if ((c == ',' && !in_quotes) || (c == '\r' && !in_quotes)) {
            if (field == "\\N") field.clear();
            fields.push_back(trim(field));
            field.clear();
        } else {
            field += c;
        }
    }
    if (field == "\\N") field.clear();
    fields.push_back(trim(field));
    return fields;
}

int safe_stoi(const std::string& s, int def = 0) {
    if (s.empty()) return def;
    try {
        return std::stoi(s);
    } catch (...) {
        return def;
    }
}

double safe_stod(const std::string& s, double def = 0) {
    if (s.empty()) return def;
    try {
        return std::stod(s);
    } catch (...) {
        return def;
    }
}

// ----- Haversine distance (miles) -----
double haversine_miles(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) {
    double lat1 = lat1_deg * M_PI / 180.0;
    double lon1 = lon1_deg * M_PI / 180.0;
    double lat2 = lat2_deg * M_PI / 180.0;
    double lon2 = lon2_deg * M_PI / 180.0;
    double dlat = lat2 - lat1;
    double dlon = lon2 - lon1;
    double a = std::sin(dlat/2)*std::sin(dlat/2) +
               std::cos(lat1)*std::cos(lat2)*std::sin(dlon/2)*std::sin(dlon/2);
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1-a));
    return EARTH_RADIUS_MILES * c;
}

// ----- Data store -----
struct Database {
    std::unordered_map<int, Airport> airports_by_id;
    std::unordered_map<std::string, Airport> airports_by_iata;
    std::unordered_map<std::string, std::vector<Airport>> airports_by_icao;
    std::unordered_map<int, Airline> airlines_by_id;
    std::unordered_map<std::string, Airline> airlines_by_iata;
    std::vector<Route> routes;

    std::string data_dir;
    int next_upload_id = -1;  // user-added airports get -1, -2, -3, ...

    void set_data_dir(const std::string& dir) { data_dir = dir; }

    // Add a single airport (e.g. from upload). If IATA already exists, overwrite (no duplicate).
    int add_airport(Airport& a) {
        if (!a.iata.empty()) {
            auto it = airports_by_iata.find(a.iata);
            if (it != airports_by_iata.end()) {
                a.id = it->second.id;
                airports_by_id[a.id] = a;
                airports_by_iata[a.iata] = a;
                if (!a.icao.empty()) airports_by_icao[a.icao].push_back(a);
                return a.id;
            }
        }
        a.id = next_upload_id--;
        if (a.iata.empty()) a.iata = "U" + std::to_string(-a.id);
        airports_by_id[a.id] = a;
        airports_by_iata[a.iata] = a;
        if (!a.icao.empty()) airports_by_icao[a.icao].push_back(a);
        return a.id;
    }

    // Remove an airport by IATA and also remove any routes that touch it.
    // Returns true if the airport existed. removed_routes_out is the number of routes removed.
    bool remove_airport_by_iata(const std::string& code, int& removed_routes_out) {
        removed_routes_out = 0;
        auto it = airports_by_iata.find(code);
        if (it == airports_by_iata.end()) return false;
        int airport_id = it->second.id;
        airports_by_iata.erase(it);
        auto itId = airports_by_id.find(airport_id);
        if (itId != airports_by_id.end()) {
            airports_by_id.erase(itId);
        }
        // Remove from airports_by_icao
        for (auto ico = airports_by_icao.begin(); ico != airports_by_icao.end(); ) {
            auto& vec = ico->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(), [airport_id](const Airport& a){ return a.id == airport_id; }), vec.end());
            if (vec.empty()) ico = airports_by_icao.erase(ico);
            else ++ico;
        }
        // Remove routes where this airport is source or destination
        auto oldSize = routes.size();
        routes.erase(std::remove_if(routes.begin(), routes.end(), [airport_id](const Route& r){
            return r.src_airport_id == airport_id || r.dest_airport_id == airport_id;
        }), routes.end());
        removed_routes_out = static_cast<int>(oldSize - routes.size());
        return true;
    }

    // Redact selected fields on an airport identified by IATA.
    // If a field flag is true, that field is set to "[REDACTED]".
    bool redact_airport(const std::string& code, bool redact_name, bool redact_city, bool redact_country) {
        auto it = airports_by_iata.find(code);
        if (it == airports_by_iata.end()) return false;
        Airport a = it->second;
        if (redact_name) a.name = "[REDACTED]";
        if (redact_city) a.city = "[REDACTED]";
        if (redact_country) a.country = "[REDACTED]";
        // Write back into maps
        airports_by_iata[code] = a;
        auto itId = airports_by_id.find(a.id);
        if (itId != airports_by_id.end()) {
            itId->second = a;
        }
        // airports_by_icao contains copies; we leave them as-is because they are not used for display
        return true;
    }

    bool load_airports() {
        std::string path = data_dir + "/airports.dat";
        std::ifstream f(path);
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto F = parse_csv_line(line);
            if (F.size() < 9) continue;
            Airport a;
            a.id = safe_stoi(F[0]);
            a.name = F.size() > 1 ? F[1] : "";
            a.city = F.size() > 2 ? F[2] : "";
            a.country = F.size() > 3 ? F[3] : "";
            a.iata = F.size() > 4 ? F[4] : "";
            a.icao = F.size() > 5 ? F[5] : "";
            a.lat = safe_stod(F.size() > 6 ? F[6] : "");
            a.lon = safe_stod(F.size() > 7 ? F[7] : "");
            a.altitude = safe_stoi(F.size() > 8 ? F[8] : "");
            airports_by_id[a.id] = a;
            if (!a.iata.empty()) airports_by_iata[a.iata] = a;
            if (!a.icao.empty()) airports_by_icao[a.icao].push_back(a);
        }
        return !airports_by_id.empty();
    }

    bool load_airlines() {
        std::string path = data_dir + "/airlines.dat";
        std::ifstream f(path);
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto F = parse_csv_line(line);
            if (F.size() < 2) continue;
            Airline al;
            al.id = safe_stoi(F[0]);
            al.name = F.size() > 1 ? F[1] : "";
            al.alias = F.size() > 2 ? F[2] : "";
            al.iata = F.size() > 3 ? F[3] : "";
            al.icao = F.size() > 4 ? F[4] : "";
            al.country = F.size() > 6 ? F[6] : "";
            al.active = F.size() > 7 ? F[7] : "";
            airlines_by_id[al.id] = al;
            if (!al.iata.empty()) airlines_by_iata[al.iata] = al;
        }
        return !airlines_by_id.empty();
    }

    bool load_routes() {
        std::string path = data_dir + "/routes.dat";
        std::ifstream f(path);
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto F = parse_csv_line(line);
            if (F.size() < 6) continue;
            Route r;
            r.airline_code = F.size() > 0 ? F[0] : "";
            r.airline_id = safe_stoi(F.size() > 1 ? F[1] : "");
            r.src_airport = F.size() > 2 ? F[2] : "";
            r.src_airport_id = safe_stoi(F.size() > 3 ? F[3] : "");
            r.dest_airport = F.size() > 4 ? F[4] : "";
            r.dest_airport_id = safe_stoi(F.size() > 5 ? F[5] : "");
            r.codeshare = F.size() > 6 ? F[6] : "";
            r.stops = safe_stoi(F.size() > 7 ? F[7] : "0");
            r.equipment = F.size() > 8 ? F[8] : "";
            routes.push_back(r);
        }
        return !routes.empty();
    }

    bool load_all() {
        return load_airports() && load_airlines() && load_routes();
    }

    const Airport* airport_by_iata(const std::string& code) const {
        auto it = airports_by_iata.find(code);
        return it == airports_by_iata.end() ? nullptr : &it->second;
    }

    const Airport* airport_by_id(int id) const {
        auto it = airports_by_id.find(id);
        return it == airports_by_id.end() ? nullptr : &it->second;
    }

    const Airline* airline_by_iata(const std::string& code) const {
        auto it = airlines_by_iata.find(code);
        return it == airlines_by_iata.end() ? nullptr : &it->second;
    }
};

} // namespace

int main() {
    crow::SimpleApp app;
    auto db = std::make_shared<Database>();

    // Determine data directory: same dir as executable, or "data", or cwd
    std::string exe_path = ".";
#ifdef _WIN32
    // On Windows we don't have argv[0] easily in Crow main; use data relative to cwd
    db->set_data_dir("data");
#else
    db->set_data_dir("data");
#endif

    if (!db->load_all()) {
        // Try current directory
        db->set_data_dir(".");
        if (!db->load_all()) {
            fprintf(stderr, "Failed to load data. Put airports.dat, airlines.dat, routes.dat in ./data/ or current directory.\n");
            fprintf(stderr, "Download from: https://github.com/jpatokal/openflights/tree/master/data\n");
            return 1;
        }
    }
    if (db->airports_by_id.size() < 100 || db->airlines_by_id.size() < 10) {
        fprintf(stderr, "WARNING: Only %zu airports and %zu airlines loaded. For full suggestions, run data/download_data.bat to replace data files with OpenFlights CSV.\n",
                db->airports_by_id.size(), db->airlines_by_id.size());
    }

    // ----- Shared page style -----
    const std::string page_style = R"html(
* { box-sizing: border-box; }
body { font-family: 'Segoe UI', system-ui, sans-serif; max-width: 960px; margin: 0 auto; padding: 0 1.5rem 2rem; background: linear-gradient(145deg, #e0f2fe 0%, #f0f9ff 40%, #fef3c7 100%); color: #1e293b; min-height: 100vh; transition: background 0.4s ease; }
body.one-page-body { max-width: none; margin: 0; padding: 0; min-height: 100vh; background: linear-gradient(160deg, #0f172a 0%, #1e293b 35%, #334155 70%, #475569 100%); }
nav { background: linear-gradient(90deg, #0c4a6e 0%, #0369a1 50%, #0284c7 100%); padding: 0.85rem 1.5rem; margin: 0 -1.5rem 1.5rem; border-radius: 0 0 12px 12px; box-shadow: 0 4px 14px rgba(2,132,199,0.35); }
nav a { color: #e0f2fe; text-decoration: none; margin-right: 1.5rem; font-weight: 600; transition: color 0.2s ease, transform 0.2s ease; }
nav a:hover { color: #fff; transform: translateY(-1px); }
h1 { color: #0c4a6e; font-size: 1.85rem; margin-bottom: 0.5rem; }
.subtitle { color: #075985; margin-bottom: 1.5rem; opacity: 0.9; }
.card { background: rgba(255,255,255,0.95); padding: 1.35rem; margin: 1rem 0; border-radius: 12px; box-shadow: 0 2px 12px rgba(12,74,110,0.12); transition: transform 0.25s ease, box-shadow 0.25s ease; }
.card:hover { transform: translateY(-2px); box-shadow: 0 8px 24px rgba(12,74,110,0.18); }
.btn { background: linear-gradient(180deg, #0284c7 0%, #0369a1 100%); color: #fff; border: none; padding: 0.55rem 1.1rem; border-radius: 8px; cursor: pointer; font-size: 0.9375rem; font-weight: 600; transition: transform 0.2s ease, box-shadow 0.2s ease; }
.btn:hover { transform: scale(1.03); box-shadow: 0 4px 12px rgba(2,132,199,0.4); }
input[type="text"], input[type="number"] { padding: 0.55rem 0.85rem; border: 2px solid #bae6fd; border-radius: 8px; font-size: 0.9375rem; width: 12rem; transition: border-color 0.2s ease, box-shadow 0.2s ease; }
input:focus { outline: none; border-color: #0284c7; box-shadow: 0 0 0 3px rgba(2,132,199,0.2); }
label { display: inline-block; margin-right: 0.5rem; font-weight: 600; color: #0c4a6e; }
table { width: 100%; border-collapse: collapse; margin-top: 1rem; border-radius: 8px; overflow: hidden; }
th, td { text-align: left; padding: 0.6rem 0.85rem; border-bottom: 1px solid #e0f2fe; transition: background 0.2s ease; }
th { background: linear-gradient(180deg, #0ea5e9 0%, #0284c7 100%); color: #fff; font-weight: 600; }
tr:hover { background: #e0f2fe; }
.results-area { min-height: 2rem; margin-top: 1rem; transition: opacity 0.3s ease; }
.error { color: #b91c1c; background: #fef2f2; padding: 0.85rem; border-radius: 8px; margin-top: 0.5rem; border-left: 4px solid #dc2626; }
.loading { color: #0369a1; }
.home-cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 1.25rem; margin: 1.5rem 0; }
.home-cards a { display: block; background: rgba(255,255,255,0.95); padding: 1.35rem; border-radius: 12px; text-decoration: none; color: #1e293b; box-shadow: 0 2px 10px rgba(12,74,110,0.1); border-left: 5px solid #0284c7; transition: transform 0.25s ease, box-shadow 0.25s ease; }
.home-cards a:hover { transform: translateY(-4px); box-shadow: 0 8px 24px rgba(2,132,199,0.25); }
.home-cards a h3 { margin: 0 0 0.35rem; color: #0369a1; font-size: 1.15rem; }
.home-cards a p { margin: 0; color: #64748b; font-size: 0.9rem; }
.quick-form { margin-bottom: 1rem; }
.quick-form .row { margin-bottom: 0.85rem; }
.suggest-wrap { position: relative; display: inline-block; }
.suggest-box { position: absolute; left: 0; top: 100%; z-index: 100; min-width: 100%; max-width: 420px; max-height: 380px; overflow-y: auto; background: #fff; border: 2px solid #7dd3fc; border-radius: 12px; box-shadow: 0 10px 30px rgba(15,23,42,0.15); margin-top: 6px; }
.suggest-box .airtravel-list { padding: 0; margin: 0; list-style: none; }
.suggest-item { padding: 0.6rem 0.85rem; cursor: pointer; border-bottom: 1px solid #e0f2fe; transition: background 0.15s ease; }
.suggest-item:last-child { border-bottom: none; }
.suggest-item:hover, .suggest-item.active { background: #e0f2fe; }
.suggest-item small { color: #64748b; display: block; margin-top: 2px; }
.btn-secondary { background: #64748b; }
.btn-secondary:hover { background: #475569; }
#tracker-page { max-width: none; padding: 0; margin: 0; }
#tracker-layout { display: flex; height: calc(100vh - 52px); }
#map-container { flex: 1; min-height: 300px; height: 100%; }
#tracker-sidebar { width: 360px; background: linear-gradient(180deg, #f0f9ff 0%, #e0f2fe 100%); padding: 1rem; overflow-y: auto; border-left: 3px solid #0284c7; }
#tracker-sidebar h3 { color: #0c4a6e; margin-top: 0; }
#tracker-sidebar .empty { color: #64748b; font-style: italic; }
.plane-marker { background: none !important; border: none !important; cursor: pointer; color: #0369a1; text-shadow: 0 0 2px #fff; }
.plane-marker:hover { color: #0c4a6e; }
.leaflet-marker-icon { transition: transform 0.2s ease; }
.leaflet-marker-icon:hover { transform: scale(1.2); }
#tracker-sidebar table { font-size: 0.9rem; }
#tracker-sidebar h4 { color: #0c4a6e; }
/* One-page layout: left bar | map (center) | right results panel */
#one-page-wrap { display: flex; height: 100vh; margin: 0; overflow: hidden; }
#one-page-left { flex: 0 0 320px; min-width: 260px; background: linear-gradient(180deg, #f8fafc 0%, #e2e8f0 50%, #cbd5e1 100%); border-right: 1px solid #64748b; box-shadow: 4px 0 20px rgba(0,0,0,0.12); display: flex; flex-direction: column; overflow-y: auto; border-radius: 0 12px 12px 0; }
#one-page-left .one-page-left-title { font-size: 1.1rem; font-weight: 700; color: #0c4a6e; padding: 0.85rem 0.75rem 0.5rem; letter-spacing: 0.02em; border-bottom: 2px solid #94a3b8; margin-bottom: 0.25rem; }
#one-page-left .search-section { padding: 0.5rem 0.75rem; border-bottom: 1px solid #bae6fd; }
#one-page-left .search-section label { display: block; font-weight: 600; color: #0c4a6e; margin-bottom: 0.25rem; font-size: 0.9rem; }
#one-page-left .search-section input { width: 100%; box-sizing: border-box; margin-bottom: 0.2rem; padding: 0.45rem 0.6rem; font-size: 0.875rem; border-radius: 8px; }
#one-page-left .search-section .row-inline { display: flex; gap: 0.4rem; align-items: center; flex-wrap: wrap; }
#one-page-left .search-section .row-inline input { flex: 1; min-width: 70px; }
#one-page-left .search-section .btn { padding: 0.45rem 0.75rem; font-size: 0.875rem; border-radius: 8px; }
#one-page-left .op-actions-divider { height: 1px; background: #94a3b8; margin: 0.5rem 0.75rem 0; opacity: 0.6; }
#one-page-left .capstone-section { border-left: 3px solid #0284c7; background: rgba(2,132,199,0.06); margin: 0.35rem 0.75rem 0.5rem; border-radius: 0 8px 8px 0; }
#one-page-left .capstone-buttons { display: flex; flex-wrap: wrap; gap: 0.35rem; align-items: center; }
#one-page-left .capstone-buttons .btn { padding: 0.35rem 0.5rem; font-size: 0.8rem; white-space: nowrap; border-radius: 8px; }
#one-page-map { flex: 1; min-width: 200px; min-height: 300px; box-shadow: inset 0 0 0 1px rgba(255,255,255,0.08); border-radius: 8px; margin: 8px 4px; border: 1px solid rgba(100,116,139,0.35); }
#one-page-right { flex: 0 0 380px; min-width: 300px; display: flex; flex-direction: column; background: linear-gradient(180deg, #e0f2fe 0%, #bae6fd 50%, #7dd3fc 100%); border-left: 1px solid #64748b; box-shadow: -4px 0 20px rgba(0,0,0,0.12); overflow: hidden; border-radius: 12px 0 0 12px; }
#one-page-right .op-results-header { margin: 0; padding: 0.75rem 1.25rem 0.5rem; font-size: 1rem; font-weight: 700; color: #0c4a6e; border-bottom: 2px solid rgba(12,74,110,0.3); background: rgba(255,255,255,0.4); flex-shrink: 0; }
#one-page-right #op-info { flex: 1; padding: 1rem 1.25rem; overflow-y: auto; overflow-x: hidden; font-size: 0.9rem; line-height: 1.5; min-height: 0; min-width: 0; }
#one-page-right #op-info .empty { color: #475569; font-style: italic; text-align: center; padding: 2rem 1rem; max-width: 280px; margin: 0 auto; }
/* Default tables in the results area, EXCEPT one-hop */
#one-page-right #op-info table:not(.onehop-table) { table-layout: fixed; width: 100%; max-width: 100%; font-size: 0.9rem; }
#one-page-right #op-info table:not(.onehop-table) td,
#one-page-right #op-info table:not(.onehop-table) th { word-break: break-word; overflow-wrap: break-word; padding: 0.5rem 0.6rem; }
/* One-hop section: own layout so text stays horizontal and readable */
#one-page-right .onehop-section {
  font-size: 0.9rem;
  margin-top: 0.75rem;
  padding: 0.5rem 0.6rem 0.6rem;
  border-radius: 10px;
  border: 1px solid #cbd5e1;
  background: rgba(15,23,42,0.04);
  overflow-x: auto;
  overflow-y: visible;
  box-sizing: border-box;
  max-width: 100%;
}
#one-page-right .onehop-section h4 {
  margin-top: 0;
  margin-bottom: 0.35rem;
  color: #0c4a6e;
  font-size: 0.95rem;
}
#one-page-right .onehop-section .onehop-table {
  table-layout: auto;
  width: 100%;
  border-collapse: collapse;
}
#one-page-right .onehop-section .onehop-table th,
#one-page-right .onehop-section .onehop-table td {
  padding: 0.25rem 0.5rem;
  font-size: 0.85rem;
  white-space: normal;
  vertical-align: middle;
}
#one-page-right .onehop-section .onehop-table .onehop-via {
  max-width: 260px;          /* Via can wrap on words */
}
#one-page-right .onehop-section .onehop-show {
  padding: 0.2rem 0.6rem;
  font-size: 0.8rem;
  white-space: nowrap;
}
.op-modal-backdrop { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.5); z-index: 9998; justify-content: center; align-items: center; }
.op-modal-backdrop.show { display: flex; }
.op-modal-box { background: #fff; border-radius: 12px; box-shadow: 0 12px 40px rgba(0,0,0,0.25); max-width: 90vw; max-height: 85vh; display: flex; flex-direction: column; overflow: hidden; }
.op-modal-box .op-modal-header { display: flex; justify-content: space-between; align-items: center; padding: 0.75rem 1rem; background: #e2e8f0; border-bottom: 1px solid #cbd5e1; flex-shrink: 0; }
.op-modal-box .op-modal-header h3 { margin: 0; font-size: 1rem; color: #0c4a6e; }
.op-modal-box .op-modal-close { background: #64748b; color: #fff; border: none; padding: 0.35rem 0.65rem; border-radius: 6px; cursor: pointer; font-size: 0.875rem; }
.op-modal-box .op-modal-close:hover { background: #475569; }
.op-modal-box .op-modal-body { padding: 1rem; overflow-y: auto; font-size: 0.9rem; }
.op-modal-search { width: 100%; max-width: 320px; padding: 0.5rem 0.75rem; margin-bottom: 0.75rem; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 0.9rem; box-sizing: border-box; }
.op-modal-search:focus { outline: none; border-color: #0284c7; }
.code-block { font-family: Consolas, 'SFMono-Regular', Menlo, Monaco, monospace; font-size: 0.75rem; white-space: pre; background: #020617; color: #e2e8f0; padding: 0.75rem; border-radius: 8px; max-height: 70vh; overflow: auto; border: 1px solid #1e293b; }
.onehop-section {
  margin-top: 0.75rem;
  padding: 0.5rem 0.6rem 0.6rem;
  border-radius: 10px;
  border: 1px solid #cbd5e1;
  background: rgba(15,23,42,0.04);
  overflow-x: auto;
  overflow-y: visible;
  box-sizing: border-box;
  max-width: 100%;
}
.onehop-section h4 {
  margin-top: 0;
  margin-bottom: 0.35rem;
  color: #0c4a6e;
  font-size: 0.95rem;
}
.onehop-section .onehop-table {
  table-layout: auto;
  width: 100%;
  border-collapse: collapse;
}
.onehop-section .onehop-table th,
.onehop-section .onehop-table td {
  padding: 0.25rem 0.5rem;
  font-size: 0.85rem;
  white-space: normal;
  vertical-align: middle;
}
.onehop-section .onehop-table .onehop-via {
  max-width: 260px;
}
.onehop-section .onehop-show {
  padding: 0.2rem 0.6rem;
  font-size: 0.8rem;
  white-space: nowrap;
}
.route-line { stroke: #0284c7; stroke-width: 2; opacity: 0.8; }
)html";

    const std::string index_html = R"html(
<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Air Travel Database</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" crossorigin="">
<style>)html" + page_style + R"html(</style></head><body class="one-page-body">
<div id="one-page-wrap">
  <div id="one-page-left">
    <div class="one-page-left-title">Air Travel</div>
    <div class="search-section">
      <label>Airports</label>
      <div class="suggest-wrap">
        <input type="text" id="op-apt-search" placeholder="e.g. SFO or San Francisco" autocomplete="off">
        <div id="op-apt-suggest" class="suggest-box" style="display:none;"><div class="airtravel-list"></div></div>
      </div>
      <button type="button" id="op-apt-btn" class="btn">Search</button>
    </div>
    <div class="search-section">
      <label>Airplanes</label>
      <div class="suggest-wrap">
        <input type="text" id="op-air-search" placeholder="e.g. AA or American" autocomplete="off">
        <div id="op-air-suggest" class="suggest-box" style="display:none;"><div class="airtravel-list"></div></div>
      </div>
      <button type="button" id="op-air-btn" class="btn">Search</button>
    </div>
    <div class="search-section">
      <label>Routes</label>
      <div class="row-inline">
        <div class="suggest-wrap" style="flex:1;">
          <input type="text" id="op-route-from" placeholder="From (e.g. SFO)" autocomplete="off">
          <div id="op-route-from-suggest" class="suggest-box" style="display:none;"><div class="airtravel-list"></div></div>
        </div>
        <div class="suggest-wrap" style="flex:1;">
          <input type="text" id="op-route-to" placeholder="To (e.g. ORD)" autocomplete="off">
          <div id="op-route-to-suggest" class="suggest-box" style="display:none;"><div class="airtravel-list"></div></div>
        </div>
      </div>
      <button type="button" id="op-route-btn" class="btn">Search</button>
    </div>
    <div class="search-section">
      <label>Top N cities (airline)</label>
      <div class="row-inline">
        <div class="suggest-wrap" style="flex:1;">
          <input type="text" id="op-topn-airline" placeholder="e.g. AA" autocomplete="off">
          <div id="op-topn-suggest" class="suggest-box" style="display:none;"><div class="airtravel-list"></div></div>
        </div>
        <label style="margin:0 0.25rem 0 0;">Top</label>
        <input type="number" id="op-topn-n" value="5" min="1" max="50" style="width:3.5rem">
      </div>
      <button type="button" id="op-topn-btn" class="btn">Search</button>
    </div>
    <div class="op-actions-divider"></div>
    <button type="button" id="op-clear-map" class="btn btn-secondary" style="margin:0.35rem 0.75rem; padding:0.4rem 0.7rem; font-size:0.875rem;">Clear map</button>
    <div class="search-section">
      <label>Upload destinations (CSV)</label>
      <p style="font-size:0.8rem; color:#64748b; margin:0.2rem 0 0.35rem 0;">Format: name,city,country,iata,lat,lon</p>
      <div style="display:flex; gap:0.35rem; align-items:center; flex-wrap:wrap;">
        <input type="file" id="op-upload-file" accept=".csv,text/csv" style="font-size:0.85rem;">
        <button type="button" id="op-upload-btn" class="btn btn-secondary">Upload</button>
      </div>
    </div>
    <div class="search-section capstone-section" style="border-bottom:none; padding:0.5rem 0.75rem 0.6rem;">
      <label style="margin-bottom:0.25rem;">Capstone</label>
      <div class="capstone-buttons">
        <button type="button" id="op-getid-btn" class="btn btn-secondary">Get ID</button>
        <button type="button" id="op-report-airlines-btn" class="btn btn-secondary">All Airlines</button>
        <button type="button" id="op-report-airports-btn" class="btn btn-secondary">All Airports</button>
        <button type="button" id="op-getcode-btn" class="btn btn-secondary">Get Code</button>
      </div>
      <div style="margin-top:0.5rem; font-size:0.8rem; color:#0f172a;">
        <div style="margin-bottom:0.25rem; font-weight:600;">Admin – Airports (in-memory only)</div>
        <div style="display:flex; flex-direction:column; gap:0.25rem;">
          <input type="text" id="admin-airport-iata" placeholder="IATA (e.g. SFO)" style="font-size:0.8rem; padding:0.3rem 0.4rem;">
          <input type="text" id="admin-airport-name" placeholder="Name" style="font-size:0.8rem; padding:0.3rem 0.4rem;">
          <input type="text" id="admin-airport-city" placeholder="City" style="font-size:0.8rem; padding:0.3rem 0.4rem;">
          <input type="text" id="admin-airport-country" placeholder="Country" style="font-size:0.8rem; padding:0.3rem 0.4rem;">
          <div style="display:flex; gap:0.25rem; flex-wrap:wrap;">
            <button type="button" id="admin-airport-save" class="btn btn-secondary" style="font-size:0.75rem; padding:0.2rem 0.5rem;">Save/Update</button>
            <button type="button" id="admin-airport-delete" class="btn btn-secondary" style="font-size:0.75rem; padding:0.2rem 0.5rem;">Delete</button>
            <button type="button" id="admin-airport-redact" class="btn btn-secondary" style="font-size:0.75rem; padding:0.2rem 0.5rem;">Redact name/city/country</button>
          </div>
        </div>
      </div>
    </div>
  </div>
  <div id="one-page-map"></div>
  <div id="one-page-right">
    <h2 class="op-results-header">Results</h2>
    <div id="op-info">
      <p class="empty">Your search results will appear here. Use the controls on the left to search airports, airplanes, routes, or top cities—the map will zoom to the result and show details here.</p>
    </div>
  </div>
</div>
<div id="op-modal-backdrop" class="op-modal-backdrop" role="dialog" aria-modal="true" aria-labelledby="op-modal-title">
  <div class="op-modal-box">
    <div class="op-modal-header">
      <h3 id="op-modal-title">Result</h3>
      <button type="button" class="op-modal-close" id="op-modal-close" aria-label="Close">Close</button>
    </div>
    <div class="op-modal-body" id="op-modal-body"></div>
  </div>
</div>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js" crossorigin=""></script>
<script>
(function(){
  var STORAGE_KEY = 'airTravelLastSearch';
  var mapEl = document.getElementById('one-page-map');
  if (!mapEl) return;
  var worldBounds = L.latLngBounds([[-85, -180], [85, 180]]);
  var map = L.map('one-page-map', {
    worldCopyJump: false,
    minZoom: 2,
    maxBounds: worldBounds,
    maxBoundsViscosity: 1
  }).setView([20, 0], 2);
  map.setMaxBounds(worldBounds);
  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution: '&copy; OpenStreetMap',
    noWrap: true,
    maxBounds: worldBounds
  }).addTo(map);
  setTimeout(function(){ map.invalidateSize(); }, 150);
  var markerLayer = L.layerGroup().addTo(map);
  var routeLineLayer = L.layerGroup().addTo(map);
  var planeIcon = L.divIcon({ className: 'plane-marker', html: '<span style="font-size:18px">&#9992;</span>', iconSize: [24,24], iconAnchor: [12,12] });
  var MAX_MARKERS = 60;
  function clearMapOverlays(){ markerLayer.clearLayers(); routeLineLayer.clearLayers(); }
  function addMarker(lat, lon, title, popup){
    var m = L.marker([lat, lon], { icon: planeIcon });
    m.bindTooltip(title || '', { permanent: false });
    if (popup) m.bindPopup(popup);
    markerLayer.addLayer(m);
    return m;
  }
  function flyToWorldThen(callback){
    map.flyTo([20, 0], 2, { duration: 0.4 });
    map.once('moveend', function(){ setTimeout(callback, 50); });
  }
  function fitBoundsFromList(list, maxZ){
    if (!list || list.length === 0) return;
    var bounds = L.latLngBounds(list.map(function(a){ return [parseFloat(a.lat), parseFloat(a.lon)]; }));
    var mz = (maxZ != null ? maxZ : 10);
    map.fitBounds(bounds, { padding: [20,20], maxZoom: mz });
  }
  function flyToBounds(bounds, maxZ){
    if (!bounds || bounds.isEmpty()) return;
    var center = bounds.getCenter();
    var zoom = Math.min(map.getBoundsZoom(bounds, false, [20,20]), maxZ != null ? maxZ : 10);
    map.flyTo(center, zoom, { duration: 0.55 });
  }
  function drawRoute(fromLat, fromLon, toLat, toLon){
    routeLineLayer.clearLayers();
    var line = L.polyline([[fromLat, fromLon], [toLat, toLon]], { color: '#0284c7', weight: 3, opacity: 0.8 });
    routeLineLayer.addLayer(line);
  }
  function esc(s){ if (s==null||s===undefined) return ''; var x=String(s); return x.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }
  function setInfo(html){ var el = document.getElementById('op-info'); if (el) el.innerHTML = html || ''; }
  function setInfoLoading(){ setInfo('<span class="loading">Loading...</span>'); }
  function setInfoError(msg){ setInfo('<p class="error">' + esc(msg || 'Request failed.') + '</p>'); }
  function showModal(title, html){ var t=document.getElementById('op-modal-title'); var b=document.getElementById('op-modal-body'); var back=document.getElementById('op-modal-backdrop'); if(t)t.textContent=title||'Result'; if(b){ b.innerHTML=html||''; b.scrollTop=0; } if(back)back.classList.add('show'); attachModalSearch(); var searchEl=document.getElementById('op-modal-search'); if(searchEl)searchEl.focus(); }
  function closeModal(){ var back=document.getElementById('op-modal-backdrop'); if(back)back.classList.remove('show'); }
  function attachModalSearch(){ var searchEl=document.getElementById('op-modal-search'); var tableEl=document.getElementById('op-modal-table'); var countEl=document.getElementById('op-modal-filter-count'); if(!searchEl||!tableEl)return; function updateCount(){ var rows=tableEl.querySelectorAll('tbody tr'); var vis=0; rows.forEach(function(tr){ if(tr.style.display!=='none')vis++; }); if(countEl)countEl.textContent=(rows.length===vis)?(rows.length+' total.'):('Showing '+vis+' of '+rows.length+'.'); } searchEl.oninput=function(){ var q=this.value.trim().toLowerCase(); tableEl.querySelectorAll('tbody tr').forEach(function(tr){ tr.style.display=(q===''||tr.textContent.toLowerCase().indexOf(q)!==-1)?'':'none'; }); updateCount(); }; updateCount(); }
  document.getElementById('op-modal-close').addEventListener('click', closeModal);
  document.getElementById('op-modal-backdrop').addEventListener('click', function(e){ if (e.target === this) closeModal(); });
  document.addEventListener('keydown', function(e){ if (e.key==='Escape'){ var back=document.getElementById('op-modal-backdrop'); if(back&&back.classList.contains('show')) closeModal(); } });
  function saveLastSearch(type){ try { localStorage.setItem(STORAGE_KEY, type); } catch(e) {} }
  function suggest(inputId, boxId, type){
    var input = document.getElementById(inputId);
    var box = document.getElementById(boxId);
    if (!input || !box) return;
    var timer;
    function show(){
      var q = input.value.trim();
      clearTimeout(timer);
      timer = setTimeout(function(){
        var url = type === 'airlines' ? '/api/suggest/airlines' : '/api/suggest/airports';
        fetch(url + '?q=' + encodeURIComponent(q)).then(function(r){ if(!r.ok)throw new Error(r.statusText); return r.json(); }).then(function(d){
          var raw = (d && d.suggestions) ? d.suggestions : [];
          var list = raw.filter(function(s){ return s && (s.label || s.value); });
          var listEl = box.querySelector('.airtravel-list');
          if (!listEl) return;
          if (list.length === 0){ box.style.display = 'none'; listEl.innerHTML = ''; return; }
          listEl.innerHTML = list.map(function(s){ var v = (s.value != null ? String(s.value) : ''); var lbl = (s.label != null ? String(s.label) : v); return '<div class="suggest-item" data-value="' + v.replace(/"/g,'&quot;') + '">' + lbl.replace(/</g,'&lt;') + '</div>'; }).join('');
          box.style.display = 'block';
          listEl.querySelectorAll('.suggest-item').forEach(function(el){ el.addEventListener('click', function(){ input.value = this.getAttribute('data-value'); box.style.display = 'none'; }); });
        }).catch(function(){ box.style.display = 'none'; });
      }, q.length === 0 ? 0 : 180);
    }
    input.addEventListener('input', show);
    input.addEventListener('focus', show);
    document.addEventListener('click', function(e){ if (!input.contains(e.target) && !box.contains(e.target)) box.style.display = 'none'; });
  }
  suggest('op-apt-search','op-apt-suggest','airports');
  suggest('op-air-search','op-air-suggest','airlines');
  suggest('op-route-from','op-route-from-suggest','airports');
  suggest('op-route-to','op-route-to-suggest','airports');
  suggest('op-topn-airline','op-topn-suggest','airlines');
  document.getElementById('op-apt-search').addEventListener('keydown', function(e){ if (e.key === 'Enter'){ e.preventDefault(); document.getElementById('op-apt-btn').click(); } });
  document.getElementById('op-air-search').addEventListener('keydown', function(e){ if (e.key === 'Enter'){ e.preventDefault(); document.getElementById('op-air-btn').click(); } });
  document.getElementById('op-route-from').addEventListener('keydown', function(e){ if (e.key === 'Enter'){ e.preventDefault(); document.getElementById('op-route-btn').click(); } });
  document.getElementById('op-route-to').addEventListener('keydown', function(e){ if (e.key === 'Enter'){ e.preventDefault(); document.getElementById('op-route-btn').click(); } });
  document.getElementById('op-topn-airline').addEventListener('keydown', function(e){ if (e.key === 'Enter'){ e.preventDefault(); document.getElementById('op-topn-btn').click(); } });
  document.getElementById('op-topn-n').addEventListener('keydown', function(e){ if (e.key === 'Enter'){ e.preventDefault(); document.getElementById('op-topn-btn').click(); } });
  document.getElementById('op-clear-map').addEventListener('click', function(){
    clearMapOverlays();
    map.flyTo([20, 0], 2, { duration: 0.35 });
    setInfo('<p class="empty">Search an airport, airplane, route, or top cities above. The map will zoom to the result and show details here.</p>');
  });
)html" R"html2(
  document.getElementById('op-apt-btn').addEventListener('click', function(){
    var q = document.getElementById('op-apt-search').value.trim();
    if (!q) return;
    setInfoLoading();
    clearMapOverlays();
    saveLastSearch('airports');
    fetch('/api/airports?search=' + encodeURIComponent(q)).then(function(r){ if(!r.ok)throw new Error(r.statusText||'Request failed'); return r.json(); }).then(function(d){
      var arr = d.airports || [];
      if (arr.length === 0){ setInfoError('No airports found.'); return; }
      var apt = arr[0];
      var code = (apt.iata || apt.icao || '').trim();
      if (arr.length > 1 && q.length <= 3){ var qu = q.toUpperCase(); for (var i = 0; i < arr.length; i++){ if ((arr[i].iata || '') === qu){ apt = arr[i]; code = arr[i].iata || ''; break; } } }
      var lat = parseFloat(apt.lat), lon = parseFloat(apt.lon);
      if (!isNaN(lat) && !isNaN(lon)){
        addMarker(lat, lon, (apt.name||'') + ' (' + (code||'') + ')');
        flyToWorldThen(function(){ map.flyTo([lat, lon], 10, { duration: 0.5 }); });
      }
      if (!code){ setInfo('<h4>' + esc(apt.name) + '</h4><p>' + esc(apt.city) + ', ' + esc(apt.country) + '</p><p>Lat/Lon: ' + (apt.lat!=null?apt.lat:'') + ', ' + (apt.lon!=null?apt.lon:'') + '</p>'); return; }
      Promise.all([ fetch('/api/airport/' + encodeURIComponent(code)).then(function(r){ if(!r.ok)throw new Error(r.statusText||'Request failed'); return r.json(); }), fetch('/api/airlines_at/' + encodeURIComponent(code)).then(function(r){ if(!r.ok)throw new Error(r.statusText||'Request failed'); return r.json(); }) ]).then(function(results){
        var ad = results[0];
        var al = results[1];
        var html = '<h4>' + esc(ad.name || apt.name || code) + '</h4>';
        html += '<p><strong>' + esc(ad.city) + ', ' + esc(ad.country) + '</strong></p>';
        html += '<p>IATA: ' + esc(ad.iata) + ' &nbsp; ICAO: ' + esc(ad.icao) + '</p>';
        html += '<p>Lat/Lon: ' + (ad.lat != null ? ad.lat : '') + ', ' + (ad.lon != null ? ad.lon : '') + '</p>';
        html += '<h4 style="margin-top:0.75rem">Airlines serving this airport</h4>';
        var airlines = (al && al.airlines) ? al.airlines : [];
        if (airlines.length === 0) html += '<p>No route data.</p>';
        else { html += '<table><thead><tr><th>Code</th><th>Name</th><th>Routes</th></tr></thead><tbody>'; airlines.slice(0, 25).forEach(function(x){ html += '<tr><td>' + esc(x.airline_code) + '</td><td>' + esc(x.airline_name) + '</td><td>' + (x.routes_count||0) + '</td></tr>'; }); html += '</tbody></table>'; }
        setInfo(html);
      }).catch(function(){ setInfoError(); });
    }).catch(function(){ setInfoError(); });
  });
  document.getElementById('op-air-btn').addEventListener('click', function(){
    var q = document.getElementById('op-air-search').value.trim();
    if (!q) return;
    setInfoLoading();
    clearMapOverlays();
    saveLastSearch('airlines');
    fetch('/api/airlines?search=' + encodeURIComponent(q)).then(function(r){ if(!r.ok)throw new Error(r.statusText||'Request failed'); return r.json(); }).then(function(d){
      var arr = d.airlines || [];
      if (arr.length === 0){ setInfoError('No airlines found.'); return; }
      var air = arr[0];
      var code = air.iata || q.toUpperCase().slice(0, 3);
      if (air.iata) code = air.iata;
      fetch('/api/airline_airports/' + encodeURIComponent(code)).then(function(r){ if(!r.ok)throw new Error(r.statusText||'Request failed'); return r.json(); }).then(function(d2){
        var ports = (d2 && d2.airports) ? d2.airports : [];
        var toShow = ports.length > MAX_MARKERS ? ports.slice(0, MAX_MARKERS) : ports;
        var boundsPoints = [];
        toShow.forEach(function(a){ var lat = parseFloat(a.lat), lon = parseFloat(a.lon); if (!isNaN(lat) && !isNaN(lon)){ addMarker(lat, lon, (a.iata||'') + ' ' + (a.name||'')); boundsPoints.push([lat, lon]); } });
        if (boundsPoints.length > 0){
          var b = L.latLngBounds(boundsPoints);
          flyToWorldThen(function(){ flyToBounds(b, 12); });
        }
        var html = '<h4>' + esc(air.name) + '</h4>';
        html += '<p><strong>IATA:</strong> ' + esc(air.iata) + ' &nbsp; <strong>Country:</strong> ' + esc(air.country) + '</p>';
        html += '<h4 style="margin-top:0.75rem">Airports served (' + ports.length + ')</h4>';
        if (ports.length > MAX_MARKERS) html += '<p style="font-size:0.85rem;color:#64748b;">Showing first ' + MAX_MARKERS + ' on map.</p>';
        if (ports.length === 0) html += '<p>No route data for this airline.</p>';
        else {
          html += '<table><thead><tr><th>IATA</th><th>Airport</th><th># Routes</th><th>Show routes</th></tr></thead><tbody>';
          ports.slice(0, 30).forEach(function(a){
            html += '<tr>';
            html += '<td>' + esc(a.iata) + '</td>';
            html += '<td>' + esc(a.name) + '</td>';
            html += '<td>' + (a.routes_count!=null?a.routes_count:'') + '</td>';
            html += '<td><button type="button" class="btn btn-secondary airline-routes-show" ' +
                    'data-airline="' + esc(air.iata || '') + '" data-airport="' + esc(a.iata || '') + '">Show</button></td>';
            html += '</tr>';
          });
          html += '</tbody></table>';
        }
        setInfo(html);
      }).catch(function(){ setInfoError(); });
    }).catch(function(){ setInfoError(); });
  });
  document.getElementById('op-route-btn').addEventListener('click', function(){
    var fromInput = document.getElementById('op-route-from').value.trim();
    var toInput = document.getElementById('op-route-to').value.trim();
    var from = fromInput.toUpperCase() || 'SFO';
    var to = toInput.toUpperCase() || 'ORD';
    var usedDefaults = !fromInput && !toInput;
    setInfoLoading();
    clearMapOverlays();
    saveLastSearch('routes');
    if (from === to){
      fetch('/api/airport/' + encodeURIComponent(from)).then(function(r){ if(!r.ok)throw new Error(r.statusText||'Request failed'); return r.json(); }).then(function(d){
        var lat = parseFloat(d.lat), lon = parseFloat(d.lon);
        if (!isNaN(lat) && !isNaN(lon)){ addMarker(lat, lon, (d.name||'') + ' (' + from + ')'); flyToWorldThen(function(){ map.flyTo([lat, lon], 10, { duration: 0.5 }); }); }
        setInfo('<h4>Route</h4><p>From and To are the same airport.</p><p><strong>' + esc(d.name||from) + '</strong> (' + esc(from) + ')</p><p><strong>Distance:</strong> 0 miles</p>');
      }).catch(function(){ setInfoError('Airport not found: ' + from); });
      return;
    }
    fetch('/api/distance/' + encodeURIComponent(from) + '/' + encodeURIComponent(to)).then(function(r){ if(!r.ok)throw new Error(r.statusText||'Request failed'); return r.json(); }).then(function(d){
      if (d.error){ setInfoError(d.error); return; }
      var fl = parseFloat(d.from_lat), fln = parseFloat(d.from_lon), tl = parseFloat(d.to_lat), tln = parseFloat(d.to_lon);
      if (!isNaN(fl) && !isNaN(fln)) addMarker(fl, fln, d.from_name || from);
      if (!isNaN(tl) && !isNaN(tln)) addMarker(tl, tln, d.to_name || to);
      if (!isNaN(fl) && !isNaN(fln) && !isNaN(tl) && !isNaN(tln)){
        drawRoute(fl, fln, tl, tln);
        var routeBounds = L.latLngBounds([[fl, fln], [tl, tln]]);
        flyToWorldThen(function(){ flyToBounds(routeBounds, 10); });
      }
      var msg = '<h4>Route</h4><p><strong>' + esc(d.from_name||from) + '</strong> (' + esc(from) + ') &rarr; <strong>' + esc(d.to_name||to) + '</strong> (' + esc(to) + ')</p><p><strong>Distance:</strong> ' + (d.distance_miles||0) + ' miles</p>';
      if (usedDefaults) msg += '<p class="empty">Using SFO → ORD as example.</p>';
      setInfo(msg);
      // Also load one-hop (S -> X -> D) suggestions
      fetch('/api/onehop/' + encodeURIComponent(from) + '/' + encodeURIComponent(to)).then(function(r){
        return r.json().then(function(x){ if(!r.ok) throw new Error((x&&x.error)||r.statusText||'Request failed'); return x; });
      }).then(function(h){
        var routes = (h && h.routes) ? h.routes : [];
        var el = document.getElementById('op-info');
        if (!el) return;
        if (routes.length === 0){
          el.innerHTML += '<p class=\"empty\">No one-hop routes found between ' + esc(from) + ' and ' + esc(to) + '.</p>';
          return;
        }
        var html = '<div class=\"onehop-section\">';
        html += '<h4>One-hop routes (non-stop legs)</h4>';
        html += '<table class=\"onehop-table\"><thead><tr><th>Via</th><th>Al 1</th><th>Al 2</th><th>Leg 1 (mi)</th><th>Leg 2 (mi)</th><th>Total (mi)</th><th>Show</th></tr></thead><tbody>';
        routes.forEach(function(rte, idx){
          var viaLabel = (rte.via_name || rte.via || '');
          if (rte.via) viaLabel += ' (' + rte.via + ')';
          html += '<tr>';
          html += '<td class=\"onehop-via\" title=\"' + esc(viaLabel) + '\">' + esc(viaLabel) + '</td>';
          html += '<td>' + esc(rte.airline1_code||'') + '</td>';
          html += '<td>' + esc(rte.airline2_code||'') + '</td>';
          html += '<td>' + (rte.leg1_miles||0) + '</td>';
          html += '<td>' + (rte.leg2_miles||0) + '</td>';
          html += '<td>' + (rte.total_miles||0) + '</td>';
          html += '<td><button type=\"button\" class=\"btn btn-secondary onehop-show\" data-index=\"' + idx + '\">Show</button></td>';
          html += '</tr>';
        });
        html += '</tbody></table></div>';
        el.innerHTML += html;

        // Wire up click handlers to visualize chosen hop on the map
        var buttons = el.querySelectorAll('.onehop-show');
        buttons.forEach(function(btn){
          btn.addEventListener('click', function(){
            var idx = parseInt(this.getAttribute('data-index'), 10);
            if (isNaN(idx) || idx < 0 || idx >= routes.length) return;
            var rte = routes[idx];
            var viaLat = parseFloat(rte.via_lat), viaLon = parseFloat(rte.via_lon);
            var fromLat = fl, fromLon = fln;
            var toLat = tl, toLon = tln;
            if (isNaN(viaLat) || isNaN(viaLon) || isNaN(fromLat) || isNaN(fromLon) || isNaN(toLat) || isNaN(toLon)) return;

            clearMapOverlays();
            // Markers
            addMarker(fromLat, fromLon, (d.from_name || from));
            addMarker(viaLat, viaLon, (rte.via_name || rte.via || '') + (rte.via ? ' (' + rte.via + ')' : ''));
            addMarker(toLat, toLon, (d.to_name || to));

            // Two-leg polyline
            routeLineLayer.clearLayers();
            var seg1 = L.polyline([[fromLat, fromLon], [viaLat, viaLon]], { color: '#22c55e', weight: 3, opacity: 0.85 });
            var seg2 = L.polyline([[viaLat, viaLon], [toLat, toLon]], { color: '#22c55e', weight: 3, opacity: 0.85 });
            routeLineLayer.addLayer(seg1);
            routeLineLayer.addLayer(seg2);

            var bounds = L.latLngBounds([[fromLat, fromLon], [viaLat, viaLon], [toLat, toLon]]);
            flyToWorldThen(function(){ flyToBounds(bounds, 10); });
          });
        });
      }).catch(function(e){
        // Silent failure for one-hop; keep direct route result
      });
    }).catch(function(){ setInfoError(); });
  });
)html2" R"html3(
  document.getElementById('op-topn-btn').addEventListener('click', function(){
    var code = document.getElementById('op-topn-airline').value.trim().toUpperCase() || 'AA';
    var n = parseInt(document.getElementById('op-topn-n').value, 10) || 5;
    if (n < 1) n = 1;
    if (n > 50) n = 50;
    setInfoLoading();
    saveLastSearch('topcities');
    fetch('/api/top_cities/' + encodeURIComponent(code) + '/' + n).then(function(r){ if(!r.ok)throw new Error(r.statusText||'Request failed'); return r.json(); }).then(function(d){
      var arr = (d && d.cities) ? d.cities : [];
      if (arr.length === 0){ setInfo('<h4>Top ' + n + ' cities for ' + esc(code) + '</h4><p>No route data for this airline.</p>'); return; }
      var html = '<h4>Top ' + n + ' cities for ' + esc(d.airline_code || code) + '</h4>';
      html += '<table><thead><tr><th>City</th><th>Routes</th></tr></thead><tbody>';
      for (var i = 0; i < arr.length; i++) html += '<tr><td>' + esc(arr[i].city) + '</td><td>' + (arr[i].routes||0) + '</td></tr>';
      html += '</tbody></table>';
      setInfo(html);
    }).catch(function(){ setInfoError(); });
  });
  document.getElementById('op-getid-btn').addEventListener('click', function(){
    showModal('Get ID (2.3)', '<span class="loading">Loading...</span>');
    fetch('/api/getid').then(function(r){ if(!r.ok)throw new Error(r.statusText||'Request failed'); return r.json(); }).then(function(d){
      showModal('Get ID (2.3)', '<p><strong>Student ID:</strong> ' + esc(d.studentId) + '</p><p><strong>Name:</strong> ' + esc(d.name) + '</p>');
    }).catch(function(){ showModal('Get ID (2.3)', '<p class="error">Request failed.</p>'); });
  });
  document.getElementById('op-report-airlines-btn').addEventListener('click', function(){
    showModal('All Airlines (2.2a)', '<span class="loading">Loading...</span>');
    fetch('/api/report/airlines').then(function(r){ if(!r.ok)throw new Error(r.statusText||'Request failed'); return r.json(); }).then(function(d){
      var arr = (d && d.airlines) ? d.airlines : [];
      var html = '<p id="op-modal-filter-count">' + arr.length + ' total.</p><input type="text" id="op-modal-search" class="op-modal-search" placeholder="Search by IATA, name, country..." aria-label="Search by IATA, name, country" autocomplete="off">';
      html += '<table id="op-modal-table"><thead><tr><th>#</th><th>IATA</th><th>Name</th><th>Country</th></tr></thead><tbody>';
      arr.forEach(function(x, i){ html += '<tr><td>' + (i+1) + '</td><td>' + esc(x.iata) + '</td><td>' + esc(x.name) + '</td><td>' + esc(x.country) + '</td></tr>'; });
      html += '</tbody></table>';
      showModal('All Airlines (2.2a)', html);
    }).catch(function(){ showModal('All Airlines (2.2a)', '<p class="error">Request failed.</p>'); });
  });
  document.getElementById('op-report-airports-btn').addEventListener('click', function(){
    showModal('All Airports (2.2b)', '<span class="loading">Loading...</span>');
    fetch('/api/report/airports').then(function(r){ if(!r.ok)throw new Error(r.statusText||'Request failed'); return r.json(); }).then(function(d){
      var arr = (d && d.airports) ? d.airports : [];
      var html = '<p id="op-modal-filter-count">' + arr.length + ' total.</p><input type="text" id="op-modal-search" class="op-modal-search" placeholder="Search by IATA, name, city, country..." aria-label="Search by IATA, name, city, country" autocomplete="off">';
      html += '<table id="op-modal-table"><thead><tr><th>#</th><th>IATA</th><th>Name</th><th>City</th><th>Country</th></tr></thead><tbody>';
      arr.forEach(function(x, i){ html += '<tr><td>' + (i+1) + '</td><td>' + esc(x.iata) + '</td><td>' + esc(x.name) + '</td><td>' + esc(x.city) + '</td><td>' + esc(x.country) + '</td></tr>'; });
      html += '</tbody></table>';
      showModal('All Airports (2.2b)', html);
    }).catch(function(){ showModal('All Airports (2.2b)', '<p class="error">Request failed.</p>'); });
  });
  document.getElementById('op-getcode-btn').addEventListener('click', function(){
    showModal('Get Code (2.4)', '<span class="loading">Loading...</span>');
    fetch('/api/getcode').then(function(r){
      return r.text().then(function(txt){
        if (!r.ok) throw new Error(txt || r.statusText || 'Request failed');
        return txt;
      });
    }).then(function(txt){
      showModal('Get Code (2.4)', '<pre class="code-block">' + esc(txt) + '</pre>');
    }).catch(function(e){
      var msg = (e && e.message) ? e.message : 'Request failed.';
      showModal('Get Code (2.4)', '<p class="error">' + esc(msg) + '</p>');
    });
  });
  // Admin: in-memory airport add/update/delete/redact
  function adminGetAirportFields(){
    return {
      iata: (document.getElementById('admin-airport-iata').value || '').trim().toUpperCase(),
      name: (document.getElementById('admin-airport-name').value || '').trim(),
      city: (document.getElementById('admin-airport-city').value || '').trim(),
      country: (document.getElementById('admin-airport-country').value || '').trim()
    };
  }
  var adminSaveBtn = document.getElementById('admin-airport-save');
  if (adminSaveBtn){
    adminSaveBtn.addEventListener('click', function(){
      var f = adminGetAirportFields();
      if (!f.iata){ setInfoError('Enter an IATA code first.'); return; }
      fetch('/api/admin/airport', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(f)
      }).then(function(r){ return r.json().then(function(d){ if(!r.ok) throw new Error((d&&d.error)||r.statusText||'Request failed'); return d; }); }).then(function(d){
        var msg = 'Airport ' + esc(f.iata) + ' saved in memory.';
        if (d && typeof d.removed_routes === 'number'){ msg += ' Routes touched: ' + d.removed_routes + '.'; }
        setInfo('<p>' + msg + '</p>');
      }).catch(function(e){
        setInfoError(e && e.message ? e.message : 'Request failed.');
      });
    });
  }
  var adminDeleteBtn = document.getElementById('admin-airport-delete');
  if (adminDeleteBtn){
    adminDeleteBtn.addEventListener('click', function(){
      var f = adminGetAirportFields();
      if (!f.iata){ setInfoError('Enter an IATA code to delete.'); return; }
      fetch('/api/admin/airport/' + encodeURIComponent(f.iata), { method: 'DELETE' }).then(function(r){
        return r.json().then(function(d){ if(!r.ok) throw new Error((d&&d.error)||r.statusText||'Request failed'); return d; });
      }).then(function(d){
        var msg;
        if (d && d.removed){
          msg = 'Airport ' + esc(f.iata) + ' removed from memory. Removed routes: ' + (d.removed_routes||0) + '.';
        } else {
          msg = 'Airport ' + esc(f.iata) + ' not found in memory.';
        }
        setInfo('<p>' + msg + '</p>');
      }).catch(function(e){
        setInfoError(e && e.message ? e.message : 'Request failed.');
      });
    });
  }
  var adminRedactBtn = document.getElementById('admin-airport-redact');
  if (adminRedactBtn){
    adminRedactBtn.addEventListener('click', function(){
      var f = adminGetAirportFields();
      if (!f.iata){ setInfoError('Enter an IATA code to redact.'); return; }
      fetch('/api/admin/airport/' + encodeURIComponent(f.iata) + '/redact', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: true, city: true, country: true })
      }).then(function(r){
        return r.json().then(function(d){ if(!r.ok) throw new Error((d&&d.error)||r.statusText||'Request failed'); return d; });
      }).then(function(d){
        var msg = (d && d.redacted) ? 'Airport ' + esc(f.iata) + ' redacted in memory.' : 'Airport ' + esc(f.iata) + ' not found.';
        setInfo('<p>' + msg + '</p>');
      }).catch(function(e){
        setInfoError(e && e.message ? e.message : 'Request failed.');
      });
    });
  }
  // Click handler for airline->airport "Show routes" buttons
  document.addEventListener('click', function(e){
    var target = e.target;
    if (!target || !target.classList) return;
    if (!target.classList.contains('airline-routes-show')) return;
    var airline = (target.getAttribute('data-airline') || '').trim().toUpperCase();
    var airport = (target.getAttribute('data-airport') || '').trim().toUpperCase();
    if (!airline || !airport) return;
    setInfoLoading();
    clearMapOverlays();
    fetch('/api/airline_routes/' + encodeURIComponent(airline) + '/' + encodeURIComponent(airport))
      .then(function(r){
        return r.json().then(function(d){
          if (!r.ok) throw new Error((d && d.error) || r.statusText || 'Request failed');
          return d;
        });
      })
      .then(function(d){
        var routes = (d && d.routes) ? d.routes : [];
        var html = '<h4>Routes for airline ' + esc(airline) + ' touching ' + esc(airport) + '</h4>';
        if (routes.length === 0){
          html += '<p>No individual routes found.</p>';
          setInfo(html);
          return;
        }
        html += '<table><thead><tr><th>From</th><th>To</th><th>Stops</th><th>Codeshare</th></tr></thead><tbody>';
        var boundsPoints = [];
        routes.forEach(function(r){
          var src = r.src || '';
          var dest = r.dest || '';
          html += '<tr><td>' + esc(src) + '</td><td>' + esc(dest) + '</td><td>' +
                  (r.stops!=null?r.stops:'') + '</td><td>' + esc(r.codeshare||'') + '</td></tr>';
          if (r.src_lat != null && r.src_lon != null && r.dest_lat != null && r.dest_lon != null){
            var fromLat = parseFloat(r.src_lat), fromLon = parseFloat(r.src_lon);
            var toLat = parseFloat(r.dest_lat), toLon = parseFloat(r.dest_lon);
            if (!isNaN(fromLat) && !isNaN(fromLon) && !isNaN(toLat) && !isNaN(toLon)){
              // Add airplane markers at endpoints, same visual language as other route views
              addMarker(fromLat, fromLon, src || ('From ' + airline));
              addMarker(toLat, toLon, dest || ('To ' + airline));
              var seg = L.polyline([[fromLat, fromLon],[toLat, toLon]], { color: '#22c55e', weight: 2, opacity: 0.85 });
              routeLineLayer.addLayer(seg);
              boundsPoints.push([fromLat, fromLon], [toLat, toLon]);
            }
          }
        });
        html += '</tbody></table>';
        setInfo(html);
        if (boundsPoints.length > 0){
          var b = L.latLngBounds(boundsPoints);
          flyToWorldThen(function(){ flyToBounds(b, 8); });
        }
      })
      .catch(function(err){
        setInfoError(err && err.message ? err.message : 'Request failed.');
      });
  });
  document.getElementById('op-upload-btn').addEventListener('click', function(){
    var fileInput = document.getElementById('op-upload-file');
    if (!fileInput || !fileInput.files || fileInput.files.length === 0){ setInfo('<p class="error">Choose a CSV file first.</p>'); return; }
    var formData = new FormData();
    formData.append('file', fileInput.files[0]);
    setInfoLoading();
    fetch('/api/upload/destinations', { method: 'POST', body: formData }).then(function(r){ return r.json().then(function(d){ if(!r.ok) throw new Error((d&&d.error)||r.statusText||'Upload failed'); return d; }); }).then(function(d){
      var added = (d && d.added) ? d.added : 0;
      var airports = (d && d.airports) ? d.airports : [];
      clearMapOverlays();
      var boundsPoints = [];
      airports.forEach(function(a){
        var lat = parseFloat(a.lat), lon = parseFloat(a.lon);
        if (!isNaN(lat) && !isNaN(lon)){ addMarker(lat, lon, (a.name||'') + ' (' + (a.iata||'') + ')'); boundsPoints.push([lat, lon]); }
      });
      if (boundsPoints.length > 0){
        var b = L.latLngBounds(boundsPoints);
        flyToWorldThen(function(){ flyToBounds(b, 12); });
      }
      var html = '<h4>Added ' + added + ' destination(s)</h4>';
      if (airports.length > 0){ html += '<table><thead><tr><th>IATA</th><th>Name</th><th>City</th><th>Country</th></tr></thead><tbody>'; airports.forEach(function(a){ html += '<tr><td>' + esc(a.iata) + '</td><td>' + esc(a.name) + '</td><td>' + esc(a.city) + '</td><td>' + esc(a.country) + '</td></tr>'; }); html += '</tbody></table>'; }
      setInfo(html);
    }).catch(function(e){ setInfoError(e && e.message ? e.message : 'Upload failed.'); });
  });
  try {
    var last = localStorage.getItem(STORAGE_KEY);
    if (last === 'airports') document.getElementById('op-apt-search').focus();
    else if (last === 'airlines') document.getElementById('op-air-search').focus();
    else if (last === 'routes') document.getElementById('op-route-from').focus();
    else if (last === 'topcities') document.getElementById('op-topn-airline').focus();
  } catch(e) {}
})();
</script>
</body></html>
)html3";

    // ----- Routes -----
    CROW_ROUTE(app, "/")
    ([&db, &index_html]() {
        return crow::response(200, index_html);
    });

    CROW_ROUTE(app, "/airlines")
    ([&db, &page_style]() {
        std::string html = R"html(
<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Airlines</title>
<style>)html" + page_style + R"html(</style></head><body>
<nav><a href="/">Home</a><a href="/airlines">Airlines</a><a href="/airports">Airports</a><a href="/query">Query</a><a href="/tracker">Tracker</a></nav>
<h1>Airlines</h1>
<p class="subtitle">Search by airline name or IATA code (e.g. American, AA).</p>
<div class="card">
  <form id="airlines-form" class="quick-form">
    <div class="row">
      <label for="air-search">Search</label>
      <div class="suggest-wrap">
        <input type="text" id="air-search" name="search" placeholder="Click or type – full list of airlines below" autocomplete="off">
        <div id="air-suggest" class="suggest-box" style="display:none;"><div class="airtravel-list"></div></div>
      </div>
      <button type="submit" class="btn">Search</button>
    </div>
  </form>
  <div id="airlines-results" class="results-area"></div>
</div>
<script>
(function(){
  var form = document.getElementById('airlines-form');
  var input = document.getElementById('air-search');
  var suggestBox = document.getElementById('air-suggest');
  var results = document.getElementById('airlines-results');
  var debounceTimer;
  function runSearch(q) {
    results.innerHTML = '<span class="loading">Loading...</span>';
    fetch('/api/airlines?search=' + encodeURIComponent(q)).then(function(r){ return r.json(); }).then(function(d){
      var arr = d.airlines || [];
      if (arr.length === 0) { results.innerHTML = '<p>No airlines found.</p>'; return; }
      var tbl = '<table><thead><tr><th>IATA</th><th>ICAO</th><th>Name</th><th>Country</th></tr></thead><tbody>';
      for (var i = 0; i < arr.length; i++) {
        tbl += '<tr><td>' + (arr[i].iata || '') + '</td><td>' + (arr[i].icao || '') + '</td><td>' + (arr[i].name || '') + '</td><td>' + (arr[i].country || '') + '</td></tr>';
      }
      tbl += '</tbody></table>';
      results.innerHTML = '<p>' + arr.length + ' result(s)</p>' + tbl;
    }).catch(function(){ results.innerHTML = '<p class="error">Request failed.</p>'; });
  }
  function showSuggestions() {
    var q = input.value.trim();
    clearTimeout(debounceTimer);
    debounceTimer = setTimeout(function() {
      fetch('/api/suggest/airlines?q=' + encodeURIComponent(q)).then(function(r){ return r.json(); }).then(function(d){
        var raw = d && Array.isArray(d.suggestions) ? d.suggestions : [];
        var list = raw.filter(function(s){ return s && (typeof s.label === 'string' || typeof s.value === 'string'); });
        var listEl = suggestBox.querySelector('.airtravel-list');
        if (!listEl) return;
        if (list.length === 0) { suggestBox.style.display = 'none'; listEl.innerHTML = ''; return; }
        listEl.innerHTML = list.map(function(s){ var v = (s.value != null ? String(s.value) : ''); var lbl = (s.label != null ? String(s.label) : v); return '<div class="suggest-item" data-value="' + v.replace(/"/g,'&quot;') + '">' + lbl.replace(/</g,'&lt;') + '</div>'; }).join('');
        suggestBox.style.display = 'block';
        listEl.querySelectorAll('.suggest-item').forEach(function(el){ el.addEventListener('click', function(){ input.value = this.getAttribute('data-value'); suggestBox.style.display = 'none'; runSearch(input.value); }); });
      }).catch(function(){ suggestBox.style.display = 'none'; });
    }, q.length === 0 ? 0 : 180);
  }
  input.addEventListener('input', showSuggestions);
  input.addEventListener('focus', function(){ showSuggestions(); });
  document.addEventListener('click', function(e){ if (!input.contains(e.target) && !suggestBox.contains(e.target)) suggestBox.style.display = 'none'; });
  form.addEventListener('submit', function(e){ e.preventDefault(); suggestBox.style.display = 'none'; runSearch(input.value.trim()); });
  var q = (new URLSearchParams(window.location.search)).get('search');
  if (q) { input.value = q; runSearch(q); }
})();
</script>
</body></html>
)html";
        return crow::response(200, html);
    });

    CROW_ROUTE(app, "/airports")
    ([&db, &page_style]() {
        std::string html = R"html(
<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Airports</title>
<style>)html" + page_style + R"html(</style></head><body>
<nav><a href="/">Home</a><a href="/airlines">Airlines</a><a href="/airports">Airports</a><a href="/query">Query</a><a href="/tracker">Tracker</a></nav>
<h1>Airports</h1>
<p class="subtitle">Search by airport name, city, or IATA code (e.g. SFO, San Francisco).</p>
<div class="card">
  <form id="airports-form" class="quick-form">
    <div class="row">
      <label for="apt-search">Search</label>
      <div class="suggest-wrap">
        <input type="text" id="apt-search" name="search" placeholder="Click or type – full list of airports below" autocomplete="off">
        <div id="apt-suggest" class="suggest-box" style="display:none;"><div class="airtravel-list"></div></div>
      </div>
      <button type="submit" class="btn">Search</button>
    </div>
  </form>
  <div id="airports-results" class="results-area"></div>
</div>
<script>
(function(){
  var form = document.getElementById('airports-form');
  var input = document.getElementById('apt-search');
  var suggestBox = document.getElementById('apt-suggest');
  var results = document.getElementById('airports-results');
  var debounceTimer;
  function runSearch(q) {
    results.innerHTML = '<span class="loading">Loading...</span>';
    fetch('/api/airports?search=' + encodeURIComponent(q)).then(function(r){ return r.json(); }).then(function(d){
      var arr = d.airports || [];
      if (arr.length === 0) { results.innerHTML = '<p>No airports found.</p>'; return; }
      var tbl = '<table><thead><tr><th>IATA</th><th>ICAO</th><th>Name</th><th>City</th><th>Country</th></tr></thead><tbody>';
      for (var i = 0; i < arr.length; i++) {
        tbl += '<tr><td>' + (arr[i].iata || '') + '</td><td>' + (arr[i].icao || '') + '</td><td>' + (arr[i].name || '') + '</td><td>' + (arr[i].city || '') + '</td><td>' + (arr[i].country || '') + '</td></tr>';
      }
      tbl += '</tbody></table>';
      results.innerHTML = '<p>' + arr.length + ' result(s)</p>' + tbl;
    }).catch(function(){ results.innerHTML = '<p class="error">Request failed.</p>'; });
  }
  function showSuggestions() {
    var q = input.value.trim();
    clearTimeout(debounceTimer);
    debounceTimer = setTimeout(function() {
      fetch('/api/suggest/airports?q=' + encodeURIComponent(q)).then(function(r){ return r.json(); }).then(function(d){
        var raw = d && Array.isArray(d.suggestions) ? d.suggestions : [];
        var list = raw.filter(function(s){ return s && (typeof s.label === 'string' || typeof s.value === 'string'); });
        var listEl = suggestBox.querySelector('.airtravel-list');
        if (!listEl) return;
        if (list.length === 0) { suggestBox.style.display = 'none'; listEl.innerHTML = ''; return; }
        listEl.innerHTML = list.map(function(s){ var v = (s.value != null ? String(s.value) : ''); var lbl = (s.label != null ? String(s.label) : v); return '<div class="suggest-item" data-value="' + v.replace(/"/g,'&quot;') + '">' + lbl.replace(/</g,'&lt;') + '</div>'; }).join('');
        suggestBox.style.display = 'block';
        listEl.querySelectorAll('.suggest-item').forEach(function(el){ el.addEventListener('click', function(){ input.value = this.getAttribute('data-value'); suggestBox.style.display = 'none'; runSearch(input.value); }); });
      }).catch(function(){ suggestBox.style.display = 'none'; });
    }, q.length === 0 ? 0 : 180);
  }
  input.addEventListener('input', showSuggestions);
  input.addEventListener('focus', function(){ showSuggestions(); });
  document.addEventListener('click', function(e){ if (!input.contains(e.target) && !suggestBox.contains(e.target)) suggestBox.style.display = 'none'; });
  form.addEventListener('submit', function(e){ e.preventDefault(); suggestBox.style.display = 'none'; runSearch(input.value.trim()); });
  var q = (new URLSearchParams(window.location.search)).get('search');
  if (q) { input.value = q; runSearch(q); }
})();
</script>
</body></html>
)html";
        return crow::response(200, html);
    });

    CROW_ROUTE(app, "/query")
    ([&db, &page_style]() {
        std::string html = R"html(
<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Query</title>
<style>)html" + page_style + R"html(</style></head><body>
<nav><a href="/">Home</a><a href="/airlines">Airlines</a><a href="/airports">Airports</a><a href="/query">Query</a><a href="/tracker">Tracker</a></nav>
<h1>Query</h1>
<p class="subtitle">Airlines at an airport, top cities for an airline, or distance between two airports.</p>
<div class="card">
  <h3 style="margin-top:0;">Airlines at airport</h3>
  <p class="subtitle" style="margin-top:-0.5rem;">What airlines fly into an airport? Type to get suggestions.</p>
  <form id="q-airlines-at" class="quick-form">
    <div class="row">
      <label for="q-apt">Airport</label>
      <div class="suggest-wrap">
        <input type="text" id="q-apt" placeholder="e.g. SFO" autocomplete="off">
        <div id="q-apt-suggest" class="suggest-box" style="display:none;"><div class="airtravel-list"></div></div>
      </div>
      <button type="submit" class="btn">Show airlines</button>
    </div>
  </form>
  <div id="res-airlines-at" class="results-area"></div>
</div>
<div class="card">
  <h3 style="margin-top:0;">Top cities for an airline</h3>
  <p class="subtitle" style="margin-top:-0.5rem;">Show me the N cities that an airline has the most routes into.</p>
  <form id="q-top-cities" class="quick-form">
    <div class="row">
      <label for="q-airline">Airline</label>
      <div class="suggest-wrap">
        <input type="text" id="q-airline" placeholder="e.g. AA" autocomplete="off">
        <div id="q-airline-suggest" class="suggest-box" style="display:none;"><div class="airtravel-list"></div></div>
      </div>
      <label for="q-n">Top</label><input type="number" id="q-n" value="5" min="1" max="50" style="width:4rem">
      <button type="submit" class="btn">Show cities</button>
    </div>
  </form>
  <div id="res-top-cities" class="results-area"></div>
</div>
<div class="card">
  <h3 style="margin-top:0;">Distance between airports</h3>
  <p class="subtitle" style="margin-top:-0.5rem;">Flight distance in miles (GPS). Type for airport suggestions.</p>
  <form id="q-distance" class="quick-form">
    <div class="row">
      <label for="q-from">From</label>
      <div class="suggest-wrap">
        <input type="text" id="q-from" placeholder="e.g. SFO" autocomplete="off">
        <div id="q-from-suggest" class="suggest-box" style="display:none;"><div class="airtravel-list"></div></div>
      </div>
      <label for="q-to">To</label>
      <div class="suggest-wrap">
        <input type="text" id="q-to" placeholder="e.g. ORD" autocomplete="off">
        <div id="q-to-suggest" class="suggest-box" style="display:none;"><div class="airtravel-list"></div></div>
      </div>
      <button type="submit" class="btn">Get distance</button>
    </div>
  </form>
  <div id="res-distance" class="results-area"></div>
</div>
<script>
(function(){
  var params = new URLSearchParams(window.location.search);
  function el(id){ return document.getElementById(id); }
  function set(id, html){ el(id).innerHTML = html; }
  function load(id, html){ el(id).innerHTML = '<span class="loading">Loading...</span>'; }

  function attachSuggest(inputId, suggestBoxId, type) {
    var input = el(inputId);
    var box = el(suggestBoxId);
    var timer;
    function show() {
      var q = input.value.trim();
      clearTimeout(timer);
      timer = setTimeout(function() {
        var url = type === 'airlines' ? '/api/suggest/airlines' : '/api/suggest/airports';
        fetch(url + '?q=' + encodeURIComponent(q)).then(function(r){ return r.json(); }).then(function(d){
          var raw = d && Array.isArray(d.suggestions) ? d.suggestions : [];
          var list = raw.filter(function(s){ return s && (typeof s.label === 'string' || typeof s.value === 'string'); });
          var listEl = box.querySelector('.airtravel-list');
          if (!listEl) return;
          if (list.length === 0) { box.style.display = 'none'; listEl.innerHTML = ''; return; }
          listEl.innerHTML = list.map(function(s){ var v = (s.value != null ? String(s.value) : ''); var lbl = (s.label != null ? String(s.label) : v); return '<div class="suggest-item" data-value="' + v.replace(/"/g,'&quot;') + '">' + lbl.replace(/</g,'&lt;') + '</div>'; }).join('');
          box.style.display = 'block';
          listEl.querySelectorAll('.suggest-item').forEach(function(item){ item.addEventListener('click', function(){ input.value = this.getAttribute('data-value'); box.style.display = 'none'; }); });
        }).catch(function(){ box.style.display = 'none'; });
      }, q.length === 0 ? 0 : 180);
    }
    input.addEventListener('input', show);
    input.addEventListener('focus', show);
    document.addEventListener('click', function(e){ if (!input.contains(e.target) && !box.contains(e.target)) box.style.display = 'none'; });
  }
  attachSuggest('q-apt', 'q-apt-suggest', 'airports');
  attachSuggest('q-airline', 'q-airline-suggest', 'airlines');
  attachSuggest('q-from', 'q-from-suggest', 'airports');
  attachSuggest('q-to', 'q-to-suggest', 'airports');

  el('q-airlines-at').addEventListener('submit', function(e){
    e.preventDefault();
    var code = el('q-apt').value.trim().toUpperCase() || 'SFO';
    load('res-airlines-at');
    fetch('/api/airlines_at/' + encodeURIComponent(code)).then(function(r){ return r.json(); }).then(function(d){
      var arr = d.airlines || [];
      if (arr.length === 0){ set('res-airlines-at', '<p>No data for ' + code + '.</p>'); return; }
      var tbl = '<table><thead><tr><th>Airline</th><th>Name</th><th>Routes</th></tr></thead><tbody>';
      for (var i = 0; i < arr.length; i++)
        tbl += '<tr><td>' + (arr[i].airline_code || '') + '</td><td>' + (arr[i].airline_name || '') + '</td><td>' + (arr[i].routes_count || 0) + '</td></tr>';
      set('res-airlines-at', '<p>Airlines at ' + code + '</p>' + tbl + '</tbody></table>');
    }).catch(function(){ set('res-airlines-at', '<p class="error">Request failed.</p>'); });
  });

  el('q-top-cities').addEventListener('submit', function(e){
    e.preventDefault();
    var code = el('q-airline').value.trim().toUpperCase() || 'AA';
    var n = parseInt(el('q-n').value, 10) || 5;
    load('res-top-cities');
    fetch('/api/top_cities/' + encodeURIComponent(code) + '/' + n).then(function(r){ return r.json(); }).then(function(d){
      var arr = d.cities || [];
      if (arr.length === 0){ set('res-top-cities', '<p>No data for ' + code + '.</p>'); return; }
      var tbl = '<table><thead><tr><th>City</th><th>Routes</th></tr></thead><tbody>';
      for (var i = 0; i < arr.length; i++) tbl += '<tr><td>' + (arr[i].city || '') + '</td><td>' + (arr[i].routes || 0) + '</td></tr>';
      set('res-top-cities', '<p>Top ' + n + ' cities for ' + code + '</p>' + tbl + '</tbody></table>');
    }).catch(function(){ set('res-top-cities', '<p class="error">Request failed.</p>'); });
  });

  el('q-distance').addEventListener('submit', function(e){
    e.preventDefault();
    var from = el('q-from').value.trim().toUpperCase() || 'SFO';
    var to = el('q-to').value.trim().toUpperCase() || 'ORD';
    load('res-distance');
    fetch('/api/distance/' + encodeURIComponent(from) + '/' + encodeURIComponent(to)).then(function(r){ return r.json(); }).then(function(d){
      if (d.error){ set('res-distance', '<p class="error">' + d.error + '</p>'); return; }
      set('res-distance', '<p><strong>' + (d.from_name || d.from) + '</strong> to <strong>' + (d.to_name || d.to) + '</strong>: <strong>' + (d.distance_miles || 0) + ' miles</strong></p>');
    }).catch(function(){ set('res-distance', '<p class="error">Request failed.</p>'); });
  });

  if (params.get('airlines_at')){ el('q-apt').value = params.get('airlines_at'); el('q-airlines-at').dispatchEvent(new Event('submit')); }
  if (params.get('top_cities')){ var v = params.get('top_cities'); el('q-airline').value = v; if (params.get('n')) el('q-n').value = params.get('n'); el('q-top-cities').dispatchEvent(new Event('submit')); }
  if (params.get('from') && params.get('to')){ el('q-from').value = params.get('from'); el('q-to').value = params.get('to'); el('q-distance').dispatchEvent(new Event('submit')); }
})();
</script>
</body></html>
)html";
        return crow::response(200, html);
    });

    // ----- API: suggest airlines (prefix match or full list when q empty) -----
    CROW_ROUTE(app, "/api/suggest/airlines")
    ([&db](const crow::request& req) {
        std::string q = req.url_params.get("q") ? req.url_params.get("q") : "";
        crow::json::wvalue out;
        std::vector<crow::json::wvalue> arr;
        std::string qlower = q;
        std::transform(qlower.begin(), qlower.end(), qlower.begin(), ::tolower);
        std::string qupper = q;
        if (q.size() <= 3) std::transform(qupper.begin(), qupper.end(), qupper.begin(), ::toupper);
        for (const auto& p : db->airlines_by_id) {
            const Airline& al = p.second;
            bool match = q.empty();
            if (!match && !al.name.empty() && al.name.size() >= qlower.size()) {
                std::string nlower = al.name;
                std::transform(nlower.begin(), nlower.end(), nlower.begin(), ::tolower);
                if (nlower.compare(0, qlower.size(), qlower) == 0) match = true;
            }
            if (!match && !al.iata.empty() && al.iata.size() >= qupper.size() && al.iata.compare(0, qupper.size(), qupper) == 0) match = true;
            if (match) {
                crow::json::wvalue j;
                j["value"] = al.iata.empty() ? al.name : al.iata;
                j["label"] = al.name + (al.iata.empty() ? "" : " (" + al.iata + ")");
                arr.push_back(std::move(j));
            }
        }
        // #region agent log
        {
            auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::string line = "{\"location\":\"suggest_airlines\",\"message\":\"suggest\",\"data\":{\"q_len\":" + std::to_string(q.size()) + ",\"arr_size\":" + std::to_string(arr.size()) + "},\"timestamp\":" + std::to_string(ts) + ",\"sessionId\":\"debug-session\",\"hypothesisId\":\"H1\",\"runId\":\"run1\"}\n";
            std::ofstream logf(".cursor/debug.log", std::ios::app);
            if (logf) logf << line;
        }
        // #endregion
        if (arr.size() > (q.empty() ? 300 : 60)) arr.resize(q.empty() ? 300 : 60);
        out["suggestions"] = std::move(arr);
        return crow::response(200, out);
    });

    // ----- API: suggest airports (prefix match or full list when q empty) -----
    CROW_ROUTE(app, "/api/suggest/airports")
    ([&db](const crow::request& req) {
        std::string q = req.url_params.get("q") ? req.url_params.get("q") : "";
        crow::json::wvalue out;
        std::vector<crow::json::wvalue> arr;
        std::string qlower = q;
        std::transform(qlower.begin(), qlower.end(), qlower.begin(), ::tolower);
        std::string qupper = q;
        if (q.size() <= 4) std::transform(qupper.begin(), qupper.end(), qupper.begin(), ::toupper);
        for (const auto& p : db->airports_by_id) {
            const Airport& a = p.second;
            bool match = q.empty();
            if (!match && !a.name.empty() && a.name.size() >= qlower.size()) {
                std::string nlower = a.name;
                std::transform(nlower.begin(), nlower.end(), nlower.begin(), ::tolower);
                if (nlower.compare(0, qlower.size(), qlower) == 0) match = true;
            }
            if (!match && !a.city.empty() && a.city.size() >= qlower.size()) {
                std::string clower = a.city;
                std::transform(clower.begin(), clower.end(), clower.begin(), ::tolower);
                if (clower.compare(0, qlower.size(), qlower) == 0) match = true;
            }
            if (!match && !a.iata.empty() && a.iata.size() >= qupper.size() && a.iata.compare(0, qupper.size(), qupper) == 0) match = true;
            if (!match && !a.icao.empty() && a.icao.size() >= qupper.size() && a.icao.compare(0, qupper.size(), qupper) == 0) match = true;
            if (match) {
                crow::json::wvalue j;
                j["value"] = a.iata.empty() ? a.icao : a.iata;
                std::string label = a.name;
                if (!a.iata.empty()) label += " (" + a.iata + ")";
                if (!a.city.empty()) label += " – " + a.city;
                j["label"] = label;
                arr.push_back(std::move(j));
            }
        }
        // #region agent log
        {
            auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::string line = "{\"location\":\"suggest_airports\",\"message\":\"suggest\",\"data\":{\"q_len\":" + std::to_string(q.size()) + ",\"arr_size\":" + std::to_string(arr.size()) + "},\"timestamp\":" + std::to_string(ts) + ",\"sessionId\":\"debug-session\",\"hypothesisId\":\"H1\",\"runId\":\"run1\"}\n";
            std::ofstream logf(".cursor/debug.log", std::ios::app);
            if (logf) logf << line;
        }
        // #endregion
        if (arr.size() > (q.empty() ? 300 : 60)) arr.resize(q.empty() ? 300 : 60);
        out["suggestions"] = std::move(arr);
        return crow::response(200, out);
    });

    // ----- API: airlines (search) -----
    CROW_ROUTE(app, "/api/airlines")
    ([&db](const crow::request& req) {
        std::string q = req.url_params.get("search") ? req.url_params.get("search") : "";
        crow::json::wvalue out;
        out["query"] = q;
        std::vector<crow::json::wvalue> arr;
        std::string qlower = q;
        std::transform(qlower.begin(), qlower.end(), qlower.begin(), ::tolower);
        for (const auto& p : db->airlines_by_id) {
            const Airline& al = p.second;
            std::string name = al.name;
            std::string nlower = name;
            std::transform(nlower.begin(), nlower.end(), nlower.begin(), ::tolower);
            bool match = q.empty() || nlower.find(qlower) != std::string::npos ||
                        (al.iata.size() >= q.size() && al.iata.find(q) != std::string::npos);
            if (match) {
                crow::json::wvalue j;
                j["id"] = al.id;
                j["name"] = al.name;
                j["iata"] = al.iata;
                j["icao"] = al.icao;
                j["country"] = al.country;
                arr.push_back(std::move(j));
            }
        }
        if (arr.size() > 100) arr.resize(100);
        out["airlines"] = std::move(arr);
        return crow::response(200, out);
    });

    // ----- API: airlines at airport -----
    CROW_ROUTE(app, "/api/airlines_at/<string>")
    ([&db](const std::string& airport_code) {
        std::string code = airport_code;
        if (code.size() == 3) std::transform(code.begin(), code.end(), code.begin(), ::toupper);
        std::unordered_map<std::string, int> count;
        for (const Route& r : db->routes) {
            if (r.dest_airport == code || r.src_airport == code) {
                std::string ac = r.airline_code;
                if (!ac.empty()) count[ac]++;
            }
        }
        crow::json::wvalue out;
        out["airport"] = code;
        std::vector<std::pair<std::string, int>> vec(count.begin(), count.end());
        std::sort(vec.begin(), vec.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (vec.size() > 50) vec.resize(50);
        std::vector<crow::json::wvalue> arr;
        for (const auto& p : vec) {
            crow::json::wvalue j;
            j["airline_code"] = p.first;
            j["routes_count"] = p.second;
            const Airline* al = db->airline_by_iata(p.first);
            if (al) j["airline_name"] = al->name;
            arr.push_back(std::move(j));
        }
        out["airlines"] = std::move(arr);
        return crow::response(200, out);
    });

    // ----- API: top N cities for airline -----
    CROW_ROUTE(app, "/api/top_cities/<string>/<int>")
    ([&db](const std::string& airline_code, int top_n) {
        std::string code = airline_code;
        if (code.size() == 2) std::transform(code.begin(), code.end(), code.begin(), ::toupper);
        if (top_n <= 0 || top_n > 100) top_n = 10;
        std::unordered_map<std::string, int> city_routes;
        for (const Route& r : db->routes) {
            if (r.airline_code != code) continue;
            int dest_id = r.dest_airport_id;
            const Airport* ap = db->airport_by_id(dest_id);
            if (ap && !ap->city.empty())
                city_routes[ap->city]++;
        }
        std::vector<std::pair<std::string, int>> vec(city_routes.begin(), city_routes.end());
        std::sort(vec.begin(), vec.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (vec.size() > (size_t)top_n) vec.resize(top_n);
        crow::json::wvalue out;
        out["airline_code"] = code;
        std::vector<crow::json::wvalue> arr;
        for (const auto& p : vec) {
            crow::json::wvalue j;
            j["city"] = p.first;
            j["routes"] = p.second;
            arr.push_back(std::move(j));
        }
        out["cities"] = std::move(arr);
        return crow::response(200, out);
    });

    // ----- API: distance between two airports -----
    CROW_ROUTE(app, "/api/distance/<string>/<string>")
    ([&db](const std::string& from_code, const std::string& to_code) {
        std::string from = from_code, to = to_code;
        if (from.size() == 3) std::transform(from.begin(), from.end(), from.begin(), ::toupper);
        if (to.size() == 3) std::transform(to.begin(), to.end(), to.begin(), ::toupper);
        const Airport* a1 = db->airport_by_iata(from);
        const Airport* a2 = db->airport_by_iata(to);
        crow::json::wvalue out;
        out["from"] = from;
        out["to"] = to;
        if (!a1) { out["error"] = "Airport not found: " + from; return crow::response(404, out); }
        if (!a2) { out["error"] = "Airport not found: " + to; return crow::response(404, out); }
        double miles = haversine_miles(a1->lat, a1->lon, a2->lat, a2->lon);
        out["distance_miles"] = std::round(miles * 10) / 10;
        out["from_name"] = a1->name;
        out["to_name"] = a2->name;
        out["from_lat"] = a1->lat;
        out["from_lon"] = a1->lon;
        out["to_lat"] = a2->lat;
        out["to_lon"] = a2->lon;
        return crow::response(200, out);
    });

    // ----- API: one-hop routes (S -> X -> D, both legs non-stop) -----
    CROW_ROUTE(app, "/api/onehop/<string>/<string>")
    ([&db](const std::string& from_code, const std::string& to_code) {
        std::string from = from_code, to = to_code;
        if (from.size() == 3) std::transform(from.begin(), from.end(), from.begin(), ::toupper);
        if (to.size() == 3) std::transform(to.begin(), to.end(), to.begin(), ::toupper);
        const Airport* a_from = db->airport_by_iata(from);
        const Airport* a_to = db->airport_by_iata(to);
        crow::json::wvalue out;
        out["from"] = from;
        out["to"] = to;
        if (!a_from) { out["error"] = "Airport not found: " + from; return crow::response(404, out); }
        if (!a_to) { out["error"] = "Airport not found: " + to; return crow::response(404, out); }

        // Collect non-stop legs from S -> X and X -> D
        std::unordered_map<std::string, std::vector<const Route*>> firstLegs;
        std::unordered_map<std::string, std::vector<const Route*>> secondLegs;
        for (const Route& r : db->routes) {
            if (r.stops != 0) continue;
            if (r.src_airport.empty() || r.dest_airport.empty()) continue;
            if (r.src_airport == from && r.dest_airport != from && r.dest_airport != to) {
                firstLegs[r.dest_airport].push_back(&r);
            }
            if (r.dest_airport == to && r.src_airport != from && r.src_airport != to) {
                secondLegs[r.src_airport].push_back(&r);
            }
        }

        struct OneHopEntry {
            std::string via;
            std::string via_name;
            std::string via_city;
            std::string via_country;
            double via_lat{0};
            double via_lon{0};
            std::string airline1;
            std::string airline2;
            double leg1;
            double leg2;
            double total;
        };

        std::vector<OneHopEntry> entries;

        for (const auto& kv : firstLegs) {
            const std::string& via_code = kv.first;
            auto it_second = secondLegs.find(via_code);
            if (it_second == secondLegs.end()) continue;
            const Airport* via_ap = db->airport_by_iata(via_code);
            if (!via_ap) continue;
            double leg1 = haversine_miles(a_from->lat, a_from->lon, via_ap->lat, via_ap->lon);
            double leg2 = haversine_miles(via_ap->lat, via_ap->lon, a_to->lat, a_to->lon);
            double total = leg1 + leg2;
            for (const Route* r1 : kv.second) {
                for (const Route* r2 : it_second->second) {
                    OneHopEntry e;
                    e.via = via_code;
                    e.via_name = via_ap->name;
                    e.via_city = via_ap->city;
                    e.via_country = via_ap->country;
                    e.via_lat = via_ap->lat;
                    e.via_lon = via_ap->lon;
                    e.airline1 = r1->airline_code;
                    e.airline2 = r2->airline_code;
                    e.leg1 = leg1;
                    e.leg2 = leg2;
                    e.total = total;
                    entries.push_back(std::move(e));
                }
            }
        }

        std::sort(entries.begin(), entries.end(), [](const OneHopEntry& a, const OneHopEntry& b) {
            return a.total < b.total;
        });

        // Limit to avoid extremely large responses
        const std::size_t MAX_RESULTS = 500;
        if (entries.size() > MAX_RESULTS) entries.resize(MAX_RESULTS);

        std::vector<crow::json::wvalue> arr;
        for (const auto& e : entries) {
            crow::json::wvalue j;
            j["via"] = e.via;
            j["via_name"] = e.via_name;
            j["via_city"] = e.via_city;
            j["via_country"] = e.via_country;
            j["via_lat"] = e.via_lat;
            j["via_lon"] = e.via_lon;
            j["airline1_code"] = e.airline1;
            j["airline2_code"] = e.airline2;
            j["leg1_miles"] = std::round(e.leg1 * 10) / 10;
            j["leg2_miles"] = std::round(e.leg2 * 10) / 10;
            j["total_miles"] = std::round(e.total * 10) / 10;
            arr.push_back(std::move(j));
        }
        out["routes"] = std::move(arr);
        out["from_name"] = a_from->name;
        out["to_name"] = a_to->name;
        return crow::response(200, out);
    });

    // ----- API: airports served by an airline, ordered by # routes (2.1a) -----
    CROW_ROUTE(app, "/api/airline_airports/<string>")
    ([&db](const std::string& airline_code) {
        std::string code = airline_code;
        if (code.size() <= 3) std::transform(code.begin(), code.end(), code.begin(), ::toupper);
        std::unordered_map<std::string, int> airport_routes;
        for (const Route& r : db->routes) {
            if (r.airline_code != code) continue;
            if (!r.src_airport.empty()) airport_routes[r.src_airport]++;
            if (!r.dest_airport.empty()) airport_routes[r.dest_airport]++;
        }
        std::vector<std::pair<std::string, int>> vec(airport_routes.begin(), airport_routes.end());
        std::sort(vec.begin(), vec.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::vector<crow::json::wvalue> arr;
        const Airline* al = db->airline_by_iata(code);
        for (const auto& p : vec) {
            const Airport* a = db->airport_by_iata(p.first);
            if (!a || (a->lat == 0 && a->lon == 0)) continue;
            crow::json::wvalue j;
            j["iata"] = a->iata;
            j["name"] = a->name;
            j["lat"] = a->lat;
            j["lon"] = a->lon;
            j["routes_count"] = p.second;
            arr.push_back(std::move(j));
            if (arr.size() >= 200) break;
        }
        crow::json::wvalue out;
        out["airline_code"] = code;
        out["airline_name"] = al ? al->name : "";
        out["airports"] = std::move(arr);
        return crow::response(200, out);
    });

    // ----- API: report all airlines ordered by IATA (2.2a) -----
    CROW_ROUTE(app, "/api/report/airlines")
    ([&db]() {
        std::vector<std::pair<std::string, Airline>> vec;
        for (const auto& p : db->airlines_by_id) {
            if (!p.second.iata.empty())
                vec.emplace_back(p.second.iata, p.second);
        }
        std::sort(vec.begin(), vec.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<crow::json::wvalue> arr;
        for (const auto& p : vec) {
            const Airline& al = p.second;
            crow::json::wvalue j;
            j["id"] = al.id;
            j["name"] = al.name;
            j["iata"] = al.iata;
            j["icao"] = al.icao;
            j["country"] = al.country;
            arr.push_back(std::move(j));
        }
        crow::json::wvalue out;
        out["airlines"] = std::move(arr);
        return crow::response(200, out);
    });

    // ----- API: report all airports ordered by IATA (2.2b) -----
    CROW_ROUTE(app, "/api/report/airports")
    ([&db]() {
        std::vector<std::pair<std::string, Airport>> vec;
        for (const auto& p : db->airports_by_id) {
            if (!p.second.iata.empty())
                vec.emplace_back(p.second.iata, p.second);
        }
        std::sort(vec.begin(), vec.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<crow::json::wvalue> arr;
        for (const auto& p : vec) {
            const Airport& a = p.second;
            crow::json::wvalue j;
            j["id"] = a.id;
            j["name"] = a.name;
            j["city"] = a.city;
            j["country"] = a.country;
            j["iata"] = a.iata;
            j["icao"] = a.icao;
            j["lat"] = a.lat;
            j["lon"] = a.lon;
            arr.push_back(std::move(j));
        }
        crow::json::wvalue out;
        out["airports"] = std::move(arr);
        return crow::response(200, out);
    });

    // ----- API: airports (search) -----
    CROW_ROUTE(app, "/api/airports")
    ([&db](const crow::request& req) {
        std::string q = req.url_params.get("search") ? req.url_params.get("search") : "";
        crow::json::wvalue out;
        out["query"] = q;
        std::vector<crow::json::wvalue> arr;
        std::string qlower = q;
        std::transform(qlower.begin(), qlower.end(), qlower.begin(), ::tolower);
        for (const auto& p : db->airports_by_id) {
            const Airport& a = p.second;
            std::string searchable = a.name + " " + a.city + " " + a.country + " " + a.iata + " " + a.icao;
            std::string slower = searchable;
            std::transform(slower.begin(), slower.end(), slower.begin(), ::tolower);
            bool match = q.empty() || slower.find(qlower) != std::string::npos;
            if (match) {
                crow::json::wvalue j;
                j["id"] = a.id;
                j["name"] = a.name;
                j["city"] = a.city;
                j["country"] = a.country;
                j["iata"] = a.iata;
                j["icao"] = a.icao;
                j["lat"] = a.lat;
                j["lon"] = a.lon;
                arr.push_back(std::move(j));
            }
        }
        if (arr.size() > 100) arr.resize(100);
        out["airports"] = std::move(arr);
        return crow::response(200, out);
    });

    // ----- API: upload CSV of destinations (adds to datastore) -----
    app.route_dynamic("/api/upload/destinations").methods(crow::HTTPMethod::Post)
    ([&db](const crow::request& req) {
        try {
            crow::multipart::message msg(req);
            crow::multipart::part part = msg.get_part_by_name("file");
            if (part.body.empty() && part.headers.empty()) {
                crow::json::wvalue err;
                err["error"] = "No file part. Use form field name 'file'.";
                return crow::response(400, err);
            }
            std::string body = part.body;
            std::vector<crow::json::wvalue> added;
            std::istringstream stream(body);
            std::string line;
            bool first = true;
            while (std::getline(stream, line)) {
                if (line.empty()) continue;
                auto F = parse_csv_line(line);
                if (F.size() < 6) continue;
                if (first && !F.empty() && (F[0] == "name" || F[0] == "Name")) { first = false; continue; }
                first = false;
                Airport a;
                a.name = F.size() > 0 ? F[0] : "";
                a.city = F.size() > 1 ? F[1] : "";
                a.country = F.size() > 2 ? F[2] : "";
                a.iata = F.size() > 3 ? F[3] : "";
                a.lat = safe_stod(F.size() > 4 ? F[4] : "");
                a.lon = safe_stod(F.size() > 5 ? F[5] : "");
                db->add_airport(a);
                crow::json::wvalue j;
                j["iata"] = a.iata;
                j["name"] = a.name;
                j["city"] = a.city;
                j["country"] = a.country;
                j["lat"] = a.lat;
                j["lon"] = a.lon;
                added.push_back(std::move(j));
            }
            crow::json::wvalue out;
            out["added"] = static_cast<int64_t>(added.size());
            out["airports"] = std::move(added);
            return crow::response(200, out);
        } catch (const std::exception& e) {
            crow::json::wvalue err;
            err["error"] = std::string(e.what());
            return crow::response(400, err);
        }
    });

    // ----- API: routes for an airline to/from a specific airport -----
    CROW_ROUTE(app, "/api/airline_routes/<string>/<string>")
    ([&db](const std::string& airline_code_in, const std::string& airport_code_in){
        std::string airline = airline_code_in;
        std::string airport = airport_code_in;
        std::transform(airline.begin(), airline.end(), airline.begin(), ::toupper);
        std::transform(airport.begin(), airport.end(), airport.begin(), ::toupper);

        const Airline* al = db->airline_by_iata(airline);
        const Airport* ap = db->airport_by_iata(airport);

        std::vector<crow::json::wvalue> routes_json;
        for (const Route& r : db->routes) {
            if (r.airline_code != airline) continue;
            bool touches = false;
            if (!r.src_airport.empty() && r.src_airport == airport) touches = true;
            if (!r.dest_airport.empty() && r.dest_airport == airport) touches = true;
            if (!touches) continue;

            crow::json::wvalue j;
            j["airline"] = r.airline_code;
            j["src"] = r.src_airport;
            j["dest"] = r.dest_airport;
            j["stops"] = r.stops;
            j["codeshare"] = r.codeshare;

            const Airport* srcAp = db->airport_by_iata(r.src_airport);
            const Airport* dstAp = db->airport_by_iata(r.dest_airport);
            if (srcAp) {
                j["src_lat"] = srcAp->lat;
                j["src_lon"] = srcAp->lon;
            }
            if (dstAp) {
                j["dest_lat"] = dstAp->lat;
                j["dest_lon"] = dstAp->lon;
            }
            routes_json.push_back(std::move(j));
            if (routes_json.size() >= 200) break;
        }

        crow::json::wvalue out;
        out["airline"] = airline;
        out["airline_name"] = al ? al->name : "";
        out["airport"] = airport;
        out["airport_name"] = ap ? ap->name : "";
        out["routes"] = std::move(routes_json);
        return crow::response(200, out);
    });

    // ----- Admin APIs: in-memory airport add/update/delete/redact -----
    CROW_ROUTE(app, "/api/admin/airport").methods(crow::HTTPMethod::Post)
    ([&db](const crow::request& req){
        try {
            auto body = crow::json::load(req.body);
            if (!body) {
                crow::json::wvalue err;
                err["error"] = "Invalid JSON body.";
                return crow::response(400, err);
            }
            std::string iata = body["iata"].s();
            if (iata.empty()) {
                crow::json::wvalue err;
                err["error"] = "IATA is required.";
                return crow::response(400, err);
            }
            std::transform(iata.begin(), iata.end(), iata.begin(), ::toupper);
            Airport a;
            a.iata = iata;
            if (body.has("name")) a.name = body["name"].s();
            if (body.has("city")) a.city = body["city"].s();
            if (body.has("country")) a.country = body["country"].s();
            // Look for an existing airport to carry over lat/lon if not provided
            auto existing = db->airport_by_iata(iata);
            if (existing) {
                a = *existing;
                a.iata = iata;
                if (body.has("name")) a.name = body["name"].s();
                if (body.has("city")) a.city = body["city"].s();
                if (body.has("country")) a.country = body["country"].s();
            }
            int id = db->add_airport(a);
            crow::json::wvalue out;
            out["id"] = id;
            out["iata"] = a.iata;
            out["name"] = a.name;
            out["city"] = a.city;
            out["country"] = a.country;
            return crow::response(200, out);
        } catch (const std::exception& e) {
            crow::json::wvalue err;
            err["error"] = std::string(e.what());
            return crow::response(400, err);
        }
    });

    CROW_ROUTE(app, "/api/admin/airport/<string>").methods(crow::HTTPMethod::Delete)
    ([&db](const crow::request&, const std::string& code){
        std::string iata = code;
        std::transform(iata.begin(), iata.end(), iata.begin(), ::toupper);
        int removed_routes = 0;
        bool removed = db->remove_airport_by_iata(iata, removed_routes);
        crow::json::wvalue out;
        out["removed"] = removed;
        out["removed_routes"] = removed_routes;
        if (!removed) {
            out["error"] = "Airport not found.";
            return crow::response(404, out);
        }
        return crow::response(200, out);
    });

    CROW_ROUTE(app, "/api/admin/airport/<string>/redact").methods(crow::HTTPMethod::Post)
    ([&db](const crow::request& req, const std::string& code){
        std::string iata = code;
        std::transform(iata.begin(), iata.end(), iata.begin(), ::toupper);
        bool redact_name = true, redact_city = true, redact_country = true;
        auto body = crow::json::load(req.body);
        if (body) {
            if (body.has("name")) redact_name = body["name"].b();
            if (body.has("city")) redact_city = body["city"].b();
            if (body.has("country")) redact_country = body["country"].b();
        }
        bool ok = db->redact_airport(iata, redact_name, redact_city, redact_country);
        crow::json::wvalue out;
        out["redacted"] = ok;
        if (!ok) {
            out["error"] = "Airport not found.";
            return crow::response(404, out);
        }
        return crow::response(200, out);
    });

    // ----- API: Get Code (2.4) -----
    CROW_ROUTE(app, "/api/getcode")
    ([]() {
        const char* paths[] = { "src/main.cpp", "../src/main.cpp" };
        std::string code;
        for (const char* p : paths) {
            std::ifstream f(p, std::ios::binary);
            if (!f)
                continue;
            std::ostringstream ss;
            ss << f.rdbuf();
            code = ss.str();
            if (!code.empty())
                break;
        }
        if (code.empty()) {
            return crow::response(500, "Unable to load main.cpp on server.");
        }
        crow::response res(200, code);
        res.add_header("Content-Type", "text/plain; charset=utf-8");
        return res;
    });

    // ----- API: Get ID (2.3 - replace with your De Anza ID and name) -----
    // Get ID (2.3): Replace with your De Anza student ID and full name before submitting.
    CROW_ROUTE(app, "/api/getid")
    ([&db]() {
        (void)db;
        crow::json::wvalue out;
        out["studentId"] = "20651649";  // YOUR_DEANZA_ID
        out["name"] = "Javid Babayev";  // Your Full Name
        return crow::response(200, out);
    });

    // ----- API: single airline by IATA (1.1) -----
    CROW_ROUTE(app, "/api/airline/<string>")
    ([&db](const std::string& code) {
        std::string c = code;
        if (c.size() <= 3) std::transform(c.begin(), c.end(), c.begin(), ::toupper);
        const Airline* al = db->airline_by_iata(c);
        if (!al) return crow::response(404, "Not found");
        crow::json::wvalue out;
        out["id"] = al->id;
        out["name"] = al->name;
        out["alias"] = al->alias;
        out["iata"] = al->iata;
        out["icao"] = al->icao;
        out["country"] = al->country;
        out["active"] = al->active;
        return crow::response(200, out);
    });

    // ----- API: single airport (for tracker sidebar) -----
    CROW_ROUTE(app, "/api/airport/<string>")
    ([&db](const std::string& code) {
        std::string c = code;
        if (c.size() == 3) std::transform(c.begin(), c.end(), c.begin(), ::toupper);
        const Airport* a = db->airport_by_iata(c);
        if (!a) return crow::response(404, "Not found");
        crow::json::wvalue out;
        out["iata"] = a->iata;
        out["icao"] = a->icao;
        out["name"] = a->name;
        out["city"] = a->city;
        out["country"] = a->country;
        out["lat"] = a->lat;
        out["lon"] = a->lon;
        out["altitude"] = a->altitude;
        return crow::response(200, out);
    });

    // ----- API: airports for map (top by route count) -----
    CROW_ROUTE(app, "/api/map/airports")
    ([&db]() {
        std::unordered_map<int, int> route_count;
        for (const Route& r : db->routes) {
            if (r.src_airport_id > 0) route_count[r.src_airport_id]++;
            if (r.dest_airport_id > 0) route_count[r.dest_airport_id]++;
        }
        std::vector<std::pair<int, int>> vec(route_count.begin(), route_count.end());
        std::sort(vec.begin(), vec.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (vec.size() > 600) vec.resize(600);
        std::vector<crow::json::wvalue> arr;
        for (const auto& p : vec) {
            const Airport* a = db->airport_by_id(p.first);
            if (!a || a->iata.empty()) continue;
            crow::json::wvalue j;
            j["iata"] = a->iata;
            j["name"] = a->name;
            j["lat"] = a->lat;
            j["lon"] = a->lon;
            arr.push_back(std::move(j));
        }
        crow::json::wvalue out;
        out["airports"] = std::move(arr);
        return crow::response(200, out);
    });

    // ----- Tracker page: world map with airport markers (English) -----
    CROW_ROUTE(app, "/tracker")
    ([&db, &page_style]() {
        std::string html = R"html(
<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Tracker – World Map</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" crossorigin="">
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js" crossorigin=""></script>
<style>)html" + page_style + R"html(</style></head><body id="tracker-page">
<nav><a href="/">Home</a><a href="/airlines">Airlines</a><a href="/airports">Airports</a><a href="/query">Query</a><a href="/tracker">Tracker</a></nav>
<div id="tracker-layout">
  <div id="map-container"></div>
  <div id="tracker-sidebar">
    <h3>Airport &amp; airlines</h3>
    <p class="empty" id="sidebar-empty">Click a plane marker on the map to see airport details and serving airlines.</p>
    <div id="sidebar-content" style="display:none;"></div>
  </div>
</div>
<script>
(function(){
  var container = document.getElementById('map-container');
  if (!container) return;
  container.style.height = '100%';
  var map = L.map('map-container', { worldCopyJump: false }).setView([20, 0], 2);
  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { attribution: '&copy; OpenStreetMap', noWrap: true, maxBounds: [[-85,-180],[85,180]] }).addTo(map);
  setTimeout(function(){ map.invalidateSize(); }, 100);
  var sidebarEmpty = document.getElementById('sidebar-empty');
  var sidebarContent = document.getElementById('sidebar-content');
  function setSidebar(html) {
    if (sidebarEmpty) sidebarEmpty.style.display = html ? 'none' : 'block';
    if (sidebarContent) { sidebarContent.style.display = html ? 'block' : 'none'; sidebarContent.innerHTML = html || ''; }
  }
  fetch('/api/map/airports').then(function(r){ return r.json(); }).then(function(d){
    var list = d && Array.isArray(d.airports) ? d.airports : [];
    list.forEach(function(a){
      var iata = a.iata;
      var lat = parseFloat(a.lat);
      var lon = parseFloat(a.lon);
      if (isNaN(lat) || isNaN(lon)) return;
      var m = L.marker([lat, lon], { icon: L.divIcon({ className: 'plane-marker', html: '<span style="font-size:18px">&#9992;</span>', iconSize: [24,24], iconAnchor: [12,12] }) }).addTo(map);
      m.bindTooltip((a.iata || '') + ' – ' + (a.name || ''), { permanent: false });
      m.on('click', function(){
        setSidebar('<p class="loading">Loading...</p>');
        Promise.all([ fetch('/api/airport/' + encodeURIComponent(iata)).then(function(r){ return r.json(); }), fetch('/api/airlines_at/' + encodeURIComponent(iata)).then(function(r){ return r.json(); }) ]).then(function(results){
          var apt = results[0];
          var air = results[1];
          var html = '<h4>' + (apt.name || iata) + '</h4>';
          html += '<p><strong>' + (apt.city || '') + ', ' + (apt.country || '') + '</strong></p>';
          html += '<p>IATA: ' + (apt.iata || '') + ' &nbsp; ICAO: ' + (apt.icao || '') + '</p>';
          html += '<p>Lat/Lon: ' + (apt.lat || '') + ', ' + (apt.lon || '') + '</p>';
          html += '<h4 style="margin-top:1rem">Airlines serving this airport</h4>';
          var arr = air && air.airlines ? air.airlines : [];
          if (arr.length === 0) html += '<p>No route data.</p>';
          else {
            html += '<table><thead><tr><th>Airline</th><th>Name</th><th>Routes</th></tr></thead><tbody>';
            arr.slice(0, 30).forEach(function(x){ html += '<tr><td>' + (x.airline_code || '') + '</td><td>' + (x.airline_name || '') + '</td><td>' + (x.routes_count || 0) + '</td></tr>'; });
            html += '</tbody></table>';
          }
          setSidebar(html);
        }).catch(function(){ setSidebar('<p class="error">Failed to load.</p>'); });
      });
    });
  }).catch(function(){ setSidebar('<p class="error">Failed to load map data.</p>'); });
})();
</script>
</body></html>
)html";
        return crow::response(200, html);
    });

    app.port(8080).multithreaded().run();
    return 0;
}
