
#include "httplib.h"
#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <stack>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <mutex>
#include <random>

using Clock = std::chrono::system_clock;
using time_point = Clock::time_point;

enum class slot_status { Free, Occupied };
enum class vehicle_class { SUV, Saloon, Truck, Motorcycle };
enum class TxStatus { Active, Closed };
enum class payment_method { Cash, Card, Mpesa, None };

struct slot {
    std::string id;        // eg "S01"
    int distance;           // metres from entrance
    slot_status status = slot_status::Free;
};

struct transaction {
    std::string entry_id;
    std::string plate;
    vehicle_class vclass;
    std::string slot_id;
    time_point entry_time;
    time_point exit_time;
    bool has_exit = false;
    int fee = 0;
    TxStatus status = TxStatus::Active;
    payment_method method = payment_method::None;  
};

// module M4 - time and fee calculation
// rules: <=30 minutes free, <=2hours KES 50, <=4hours KES 100, <=6hours KES 300, >6hours KES 500
int calculate_fee(int minutes_parked) {
    if (minutes_parked <= 30) return 0;
    else if (minutes_parked <= 120) return 50;
    else if (minutes_parked <= 240) return 100;
    else if (minutes_parked <= 360) return 300;
    else return 500;
}

class ParkingSystem {
    public:
        ParkingSystem(int total_slots) {
            for (int i = 1; i <= total_slots; i++) {
                std::ostringstream id;
                id << "S" << std::setw(2) << std::setfill('0') << i;
                slots.push_back({id.str(), i, slot_status::Free});
            }
        }

        int free_count() const {
            int n = 0;
            for (auto& s : slots) {
                if (s.status == slot_status::Free) n++;
            }
            return n;
        }

        std::vector<slot> slots;
        std::unordered_map<std::string, transaction> active;   // plate -> active transaction
        std::vector<transaction> history;                       // closed transactions
        std::stack<std::string> last_actions;                   // undo log for operator corrections
        int next_entry_id = 1;
};

// module M3 - barrier control, shared by entry and exit
void open_barrier(const std::string& gate_label) {
    std::cout << "[" << gate_label << "] barrier raising...open.\n";
}
void close_barrier(const std::string& gate_label) {
    std::cout << "[" << gate_label << "] barrier lowering...closed.\n";
}

// module M2 - vehicle entry and registration 
bool vehicle_entry(ParkingSystem& sys, const std::string& plate_in, vehicle_class vclass) {
    if (sys.free_count() == 0) {
        std::cout << "LOT FULL - entry denied.\n";
        return false;
    }
    slot* chosen = nullptr;
    for (auto& s : sys.slots) {
        if (s.status == slot_status::Free && (!chosen || s.distance < chosen->distance)) {
            chosen = &s;
        }
    }

    std::string plate = plate_in;
    std::ostringstream eid;
    eid << "E" << std::setw(5) << std::setfill('0') << sys.next_entry_id++;
    if (plate.empty()) plate = "UNVERIFIED-" + eid.str();   // ANPR fallback

    transaction tx;
    tx.entry_id = eid.str();
    tx.plate = plate;
    tx.vclass = vclass;
    tx.slot_id = chosen->id;
    tx.entry_time = Clock::now();
    tx.status = TxStatus::Active;

    chosen->status = slot_status::Occupied;
    sys.active[plate] = tx;
    sys.last_actions.push(plate);

    std::cout << "registered " << plate << " -> slot " << chosen->id
              << " (entry ID " << tx.entry_id << ")\n";
    open_barrier("ENTRY");
    close_barrier("ENTRY");
    return true;
}

// module M5 - payment processing
bool process_payment(payment_method method, int amount_due) {
    if (amount_due == 0) return true;

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    switch (method) {
        case payment_method::Cash:
            return true;   
        case payment_method::Mpesa: {
            bool ok = dist(rng) < 0.9;
            std::cout << (ok ? "payment received.\n" : "payment failed, manual override used.\n");
            return true;   // offline override opens the barrier
        }
        case payment_method::Card: {
            bool ok = dist(rng) < 0.92;
            std::cout << (ok ? "payment approved.\n" : "payment declined.\n");
            return ok;
        }
        default: return false;
    }
}

// module M6 - vehicle exit
bool vehicle_exit(ParkingSystem& sys, const std::string& plate, int simulate_minutes_ago,
                   payment_method method, int& out_fee, int& out_minutes) {
    auto it = sys.active.find(plate);
    if (it == sys.active.end()) return false;

    transaction tx = it->second;
    time_point entry_for_calc = tx.entry_time;
    if (simulate_minutes_ago > 0) {
        entry_for_calc = Clock::now() - std::chrono::minutes(simulate_minutes_ago);
    }
    out_minutes = (int)std::chrono::duration_cast<std::chrono::minutes>(Clock::now() - entry_for_calc).count();
    out_fee = calculate_fee(out_minutes);

    if (out_fee > 0 && !process_payment(method, out_fee)) {
        return false;   // payment failed or declined, exit denied
    }

    tx.exit_time = Clock::now();
    tx.has_exit = true;
    tx.fee = out_fee;
    tx.status = TxStatus::Closed;
    tx.method = method;

    for (auto& s : sys.slots) {
        if (s.id == tx.slot_id) {
            s.status = slot_status::Free;
            break;
        }
    }
    sys.history.push_back(tx);
    sys.active.erase(it);

    open_barrier("EXIT");
    close_barrier("EXIT");
    std::cout << "exit complete. slot " << tx.slot_id << " is now free.\n";
    return true;
}

// module M7 - persistence
void save_history(const ParkingSystem& sys, const std::string& path = "transactions.csv") {
    std::ofstream out(path);
    out << "entry_id,plate,slot_id,fee,method,status\n";
    for (auto& tx : sys.history) {
        out << tx.entry_id << "," << tx.plate << "," << tx.slot_id << "," << tx.fee << ","
            << (int)tx.method << ",Closed\n";
    }
}

// module M8 - admin, reporting and rate configuration
std::string esc(const std::string& s) {
    std::string out;
    for (char c : s) { if (c == '"' || c == '\\') out += '\\'; out += c; }
    return out;
}
std::string report_json(ParkingSystem& sys) {
    int revenue = 0;
    for (auto& tx : sys.history) revenue += tx.fee;
    std::ostringstream out;
    out << "{\"revenue\":" << revenue << ",\"active\":" << sys.active.size()
        << ",\"closed\":" << sys.history.size() << "}";
    return out.str();
}
std::string board_json(ParkingSystem& sys) {
    std::ostringstream out;
    out << "{\"free\":" << sys.free_count() << ",\"total\":" << sys.slots.size() << ",\"slots\":[";
    for (size_t i = 0; i < sys.slots.size(); i++) {
        auto& s = sys.slots[i];
        out << "{\"id\":\"" << s.id << "\",\"status\":\""
            << (s.status == slot_status::Free ? "FREE" : "OCCUPIED") << "\"}";
        if (i + 1 < sys.slots.size()) out << ",";
    }
    out << "]}";
    return out.str();
}
std::string active_json(ParkingSystem& sys) {
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (auto& [plate, tx] : sys.active) {
        if (!first) out << ",";
        first = false;
        out << "{\"plate\":\"" << esc(plate) << "\",\"entryId\":\"" << tx.entry_id
            << "\",\"slotId\":\"" << tx.slot_id << "\"}";
    }
    out << "]";
    return out.str();
}

vehicle_class parse_class(const std::string& s) {
    if (s == "SUV") return vehicle_class::SUV;
    if (s == "TRUCK") return vehicle_class::Truck;
    if (s == "MOTORCYCLE") return vehicle_class::Motorcycle;
    return vehicle_class::Saloon;
}
payment_method parse_method(const std::string& s) {
    if (s == "MPESA") return payment_method::Mpesa;
    if (s == "CARD") return payment_method::Card;
    return payment_method::Cash;
}

int main() {
    ParkingSystem sys(18);     // initialise with 18 slots
    std::mutex sys_mutex;       

    httplib::Server svr;

    // serve the frontend
    svr.set_mount_point("/", "./public");

    //  menu option 1 -> GET /api/board
    svr.Get("/api/board", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(sys_mutex);
        res.set_content(board_json(sys), "application/json");
    });

    // list of parked vehicles, for the exit dropdown
    svr.Get("/api/active", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(sys_mutex);
        res.set_content(active_json(sys), "application/json");
    });

    //  POST /api/entry
    svr.Post("/api/entry", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(sys_mutex);
        std::string plate = req.get_param_value("plate");
        vehicle_class vc = parse_class(req.get_param_value("vclass"));
        bool ok = vehicle_entry(sys, plate, vc);
        if (!ok) {
            res.status = 409;
            res.set_content("{\"error\":\"LOT_FULL\"}", "application/json");
            return;
        }
        // vehicle_entry stores the record under the (possibly auto-generated) plate
        std::string finalPlate = plate.empty() ? sys.last_actions.top() : plate;
        transaction& tx = sys.active[finalPlate];
        std::ostringstream out;
        out << "{\"entryId\":\"" << tx.entry_id << "\",\"plate\":\"" << esc(tx.plate)
            << "\",\"slotId\":\"" << tx.slot_id << "\"}";
        res.set_content(out.str(), "application/json");
    });

    // POST /api/exit
    svr.Post("/api/exit", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(sys_mutex);
        std::string plate = req.get_param_value("plate");
        int sim = 0;
        if (!req.get_param_value("simulateMinutes").empty())
            sim = std::stoi(req.get_param_value("simulateMinutes"));
        payment_method method = parse_method(req.get_param_value("method"));

        int fee = 0, minutes = 0;
        bool ok = vehicle_exit(sys, plate, sim, method, fee, minutes);
        if (!ok) {
            res.status = 402;
            res.set_content("{\"error\":\"PAYMENT_OR_LOOKUP_FAILED\"}", "application/json");
            return;
        }
        std::ostringstream out;
        out << "{\"fee\":" << fee << ",\"minutes\":" << minutes << "}";
        res.set_content(out.str(), "application/json");
    });

    // GET /api/report
    svr.Get("/api/report", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(sys_mutex);
        res.set_content(report_json(sys), "application/json");
    });

    // POST /api/save
    svr.Post("/api/save", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(sys_mutex);
        save_history(sys);
        res.set_content("{\"saved\":true}", "application/json");
    });

    std::cout << "AutoPark KE server running at http://localhost:8080\n";
    svr.listen("0.0.0.0", 8080);
}
