
/*

  ____             _       _ _     _          _               
 / ___|  ___   ___(_) __ _| (_)___| |_    ___| | __ _ ___ ___ 
 \___ \ / _ \ / __| |/ _` | | / __| __|  / __| |/ _` / __/ __|
  ___) | (_) | (__| | (_| | | \__ \ |_  | (__| | (_| \__ \__ \
 |____/ \___/ \___|_|\__,_|_|_|___/\__|  \___|_|\__,_|___/___/
                                                              

*/

#ifndef __SOCIALIST_H__
#define __SOCIALIST_H__

#include <string.h>
#include <iostream>
#include <vector>
#include <array>
#include <chrono>
#include <nlohmann/json.hpp>
#include <atomic>
#include <random>
#include <mutex>

//tui
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/event.hpp>

#define HOURS 24

using namespace std;
using json = nlohmann::json;
using namespace chrono;

struct Strategy{

  vector<double> _requests;
  vector<double> _flex;
  vector<int> _durations;
  steady_clock::time_point _last_active; 
};

struct FuturePowers{

  vector<double> _powers;
  steady_clock::time_point _last_active; 
};

class Socialist{

  public:
    Socialist() : _gen(_rd()), _dis(0.1, 0.9) {
      _strategy._requests.assign(HOURS, 0.0);
      _strategy._flex.assign(HOURS, 0.0);
      _tot_powers.assign(HOURS, 0.0);
      _residuals.assign(HOURS, 0.0);
    }
    
    ~Socialist() {};

    void listen(json const &input, string topic);

    /**
     * 
     * - residuals, for the negative ones check if this -> _strategy is the one with the lowest flexibilty for the given hour. If it is, shift it
     * 
     */
    void update_strategy(); // funzione effettiva

    // funzioni di supporto
    void pop_totalPowers(); 
    void pop_totalRequest();
    void compute_residuals();

    double get_current_request();
    vector<double> get_all_requests() const {
      lock_guard<mutex> lock(const_cast<mutex&>(_data_mutex));
      return _strategy._requests;
    }
    vector<double> get_all_flex() const { 
      lock_guard<mutex> lock(const_cast<mutex&>(_data_mutex));
      return _strategy._flex; }
    void add_noise();

    /*
      _____ _   _ ___ 
     |_   _| | | |_ _|
       | | | | | || | 
       | | | |_| || | 
       |_|  \___/|___|
                      
    */
    void clear_screen() { cout << "\033[2J\033[1;1H"; }
    void display_tui(const vector<double>& powers, const vector<double>& flex, int cursor);
    void run_planner_ui(atomic<bool>& global_running);


  private:
  
    Strategy _strategy;

    vector<double> _tot_powers; // somma delle potenze per ogni ora
    vector<double> _residuals; // margini
    map<string, Strategy> _neighbours; // [load_1, strategy(due vettori)] [load_2, strategy] [load_3..]...
    // _neightbors[1] -> first
    // _neighbours[1] -> second._requests // _flex
    map<string, FuturePowers> _powers;

    bool _noise = false;
    std::vector<bool> _is_manual = std::vector<bool>(24, false);
    std::random_device _rd;
    std::mt19937 _gen;
    std::uniform_real_distribution<double> _dis;

    double _current_request = 0.0;

    mutex _data_mutex;
};


#endif