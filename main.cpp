#include <iostream>
#include <vector>
#include <unordered_map>
#include <queue>
#include <string>
#include <stack>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <random>


using Clock = std::chrono::system_clock;
using time_point = Clock::time_point;

enum class slot_status { Free, Occupied };
enum class vehicle_class { SUV, Saloon, Truck, Motorcycle };
enum class TxStatus { Active, Closed };
enum class payment_method { Cash, Card, Mpesa, None };

struct slot {
    std::string id;        //eg "SO1"
    int distance;          //meters from entrance
    slot_status status =slot_status::Free;
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


//module M4 - time and fee calculation
//rules: <=30 minutes free, <=2hours KES 50, <=4hours KES 100, <=6hours KES 300, >6hours KES 500
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
            for (int i = 1; i<=total_slots; i++) {
                std::ostringstream id;
                id << "S" << std::setw(2) << std::setfill('0') << i;
                slots.push_back({id.str(), i, slot_status::Free});

            }
        }

        int free_count() const {
            int n = 0;
            for (auto& s : slots){
                if (s.status == slot_status::Free) n++;
            }
            return n;
        }
   

        //module M1 - snapshot for the entrance display
        void print_board() const {
            std::cout << "\n=== ENTRANCE DISPLAY ===\n";
            std::cout <<free_count() << "/" << slots.size() << "slots free\n";
            if (free_count() == 0) std::cout << ">>> LOT FULL - ENTRY LOCKED <<<\n";
            for (auto& s : slots){
               std::cout << s.id << (s.status == slot_status::Free ? "[Free]" : "[Occupied]");
               std::cout << "\n";
        }    
    }

    std::vector<slot> slots;
    std::unordered_map<std::string, transaction> active;   //plate ->active transaction
    std::vector<transaction> history;                     //closed transactions
    std::stack<std::string> last_actions;                //undo lof for operator corrections
    int next_entry_id = 1;

};

//module M3- barrier control shared by entry and exit
void open_barrier(const std::string& gate_label) {
    std::cout << "[" << gate_label << "] barrier raising...open.\n";
}
void close_barrier(const std::string& gate_label) {
    std::cout << "[" << gate_label << "] barrier lowering...closed.\n";
    
}
//module M2 - vehicle entry and registration
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
if (plate.empty()) plate = "UNVERIFIED-" + eid.str();    //NPR fallback

transaction tx;
tx.entry_id = eid.str();
tx.plate = plate;
tx.vclass = vclass;
tx.slot_id = chosen->id;
tx.entry_time = Clock::now();
tx.status = TxStatus::Active;

chosen->status = slot_status::Occupied;
sys.active[plate] = tx;
sys.last_actions.push(plate);      //for undo

std::cout << "registered" << plate << " -> slot " << chosen->id
          << " (entry ID " << tx.entry_id << ")\n";
 open_barrier("ENTRY");
close_barrier("ENTRY");
return true;
}; 
//module M5 - payment processing
bool process_payment(payment_method method, int amount_due) {
    if (amount_due == 0) return true;        //free period, nothing to change

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    switch (method) {
        case payment_method::Cash: {
            std::cout << "collecting KES" << amount_due << "cash. operator confirm? (y/n): ";
            char c; std::cin >> c;
            return c == 'y' || c == 'Y';

        }
        case payment_method::Mpesa: {
            std::cout << "STK push sent for KES" << amount_due << ".....";
            bool ok = dist(rng) < 0.9;   //90% success rate
            if (ok) std::cout << "payment received.\n";
            else std::cout << "payment failed, manual override used.\n";
            return true;    //offline override still opens the barrier

        }
        case payment_method::Card: {
            std::cout << "processing card payment of KES" << amount_due << ".....";
            bool ok = dist(rng) <0.92;   //92% success rate
            std::cout << (ok ? "payment approved.\n" : "payment declined.\n");
            return ok;
        }
        default: return false;
    }
}


//module M6- vehicle exit
void vehicle_exit(ParkingSystem& sys, const std::string& plate, int simulate_minutes_ago = -1) {
    auto it = sys.active.find(plate);
    if (it == sys.active.end()) {
        std::cout << "no active transaction found for plate " << plate << " - operator lookup needed.\n";
        return;
    }
transaction tx = it->second;
time_point entry_for_calc = tx.entry_time;
if (simulate_minutes_ago > 0) {
    entry_for_calc = Clock::now() - std::chrono::minutes(simulate_minutes_ago);
}
int minutes_parked = std::chrono::duration_cast<std::chrono::minutes>(Clock::now() - entry_for_calc).count();
int fee = calculate_fee(minutes_parked);

std::cout << "duration: " << minutes_parked << " minutes, fee: KES" << fee << "\n";

payment_method method = payment_method::None;
if (fee > 0) {
    std::cout << "select payment method (1=Cash, 2=Card, 3=Mpesa): ";
    int choice; std::cin >> choice;
    switch (choice) {
        case 1: method = payment_method::Cash; break;
        case 2: method = payment_method::Card; break;
        case 3: method = payment_method::Mpesa; break;
        default: std::cout << "invalid choice, defaulting to Cash.\n"; method = payment_method::Cash; break;
    }

        if(!process_payment(method, fee)) {
            std::cout << "payment failed or declined, exit denied notify operator.\n";
            return;


                }   
            }                 
tx.exit_time = Clock::now();
tx.has_exit = true;
tx.fee = fee;
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
std::cout << "exit complete. slot" <<tx.slot_id << " is now free.\n";
};

//module M7
void save_history(const ParkingSystem& sys, const std::string& path = "transactions.csv") {
    std::ofstream out(path);
    out << "entry_id,plate,slot_id,fee,method,status\n";
    for (auto& tx : sys.history) {
        out <<tx.entry_id << "," << tx.plate << "," << tx.slot_id << "," << tx.fee << "," << (int)tx.method << "," << "Closed" << "\n";


    }
};

//Module M8 - admin,  reporting and rate configuaration
void print_report(const ParkingSystem& sys) {
    int revenue = 0;
    for (auto& tx : sys.history) revenue += tx.fee;
    std::cout <<  "\n === ADMIN REPORT ===\n";
    std::cout << "vehicles currently parked: " << sys.active.size() << "\n";
    std::cout << "completed visits: " << sys.history.size() << "\n";
    std::cout << "total revenue: KES " << revenue << "\n";
    std::cout << "rate card: <=30min free, <=2h KES50, <=4h KES100, <=6h KES300, >6h KES500\n";

};


int main() {
    ParkingSystem sys(18);     //initialise with 18 slots

    int choice = -1;
    while (choice != 0){
        std::cout << "\n--- PARKING SYSTEM MENU ---\n"
                    << "1. Show entrance display\n"
                    << "2. Register vehicle entry\n"
                    << "3. Process vehicle exit\n"
                    << "4. Show admin report\n"
                    << "0. Save and exit\n ";

                std::cin >> choice;
                
                if(choice == 1) {
                    sys.print_board();
                } else if (choice == 2) {
                    std::cin.ignore();
                    std::string plate;
                    std::cout << "Enter vehicle plate (or leave blank for unverified): ";
                    std::getline(std::cin, plate);
                    vehicle_entry(sys, plate, vehicle_class::Saloon);  //defaulting to Saloon for simplicity
                }   else if (choice == 3) {
                    std::cin.ignore();
                    std::string plate;
                    std::cout << "Enter vehicle plate for exit: ";
                    std::getline(std::cin, plate);
                    std::cout << "Simulate minutes parked (-1 = use real time): ";
                    int sim; std::cin >> sim;
                    vehicle_exit(sys, plate, sim);
                }   else if (choice == 4) {
                     print_report(sys);
                }

                }


                save_history(sys);
                std::cout << "transaction history saved. exiting.\n";
                return 0;

            }
                     
            
    


    