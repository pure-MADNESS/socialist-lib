/*

  ____             _       _ _     _          _               
 / ___|  ___   ___(_) __ _| (_)___| |_    ___| | __ _ ___ ___ 
 \___ \ / _ \ / __| |/ _` | | / __| __|  / __| |/ _` / __/ __|
  ___) | (_) | (__| | (_| | | \__ \ |_  | (__| | (_| \__ \__ \
 |____/ \___/ \___|_|\__,_|_|_|___/\__|  \___|_|\__,_|___/___/
                                                              

*/

#include "socialist.hpp"

void Socialist::listen(json const &input, string topic){

// if source node
  if(topic.rfind("source", 0) == 0){

    auto iter_sources = _powers.find(topic);
    if(input.contains("hourly")){
      
      auto const& json_array = input.at("hourly");

      if(iter_sources != _powers.end()){
        for(int i = 0; i < HOURS; ++i){
          iter_sources -> second._powers[i] = json_array.at(i).get<double>();
        }
      } else {

        FuturePowers new_hourly;

        for(size_t i = 0; i < HOURS; ++i){
          new_hourly._powers[i] = json_array.at(i).get<double>();
        }
        _powers[topic] = new_hourly;
      }
    }

  // if load node
  } else if(topic.rfind("load", 0) == 0){

    auto iter_loads = _neighbours.find(topic);
    auto now = steady_clock::now();

    if(iter_loads != _neighbours.end() && input.contains("hourly")){

      auto const& json_array = input.at("hourly");

      for(int i = 0; i < HOURS; i++){
        iter_loads -> second._requests[i] = json_array.at("requests").at(i).get<double>();
        iter_loads -> second._flex[i] = json_array.at("flexibilities").at(i).get<double>();
      }
      
    } else{

      Strategy new_load;

      auto const& json_array = input.at("hourly");
      
      for(int i = 0; i < HOURS; i++){
        new_load._requests[i] = json_array.at("requests").at(i).get<double>();
        new_load._flex[i] = json_array.at("flexibilities").at(i).get<double>();
      }
    
      _neighbours[topic] = new_load;
    }

  } else{

    return;
  }
  
}

void Socialist::pop_totalPowers() {

    _tot_powers.assign(24, 0.0);  // Svuoto per sicurezza
    
    for (const auto& [name, fp] : _powers) {
        for (int h = 0; h < 24; ++h) {
            _tot_powers[h] += fp._powers[h];
        }
    }
}


void Socialist::pop_totalRequest(){
                                  // funzione uguale a pop_totalPowers ma per le richieste
    _residuals.assign(24, 0.0);  // Svuoto per sicurezza
    
    for (const auto& [name, strategy] : _neighbours) {
        for (int h = 0; h < 24; ++h) {
            _residuals[h] += strategy._requests[h];
        }
    }
}


void Socialist::compute_residuals(){

    for (int h = 0; h < 24; ++h) {
        _residuals[h] = _tot_powers[h] - _residuals[h]; // Residuals = Total Powers - Total Requests
    }
}




void Socialist::update_strategy(){

pop_totalPowers();
pop_totalRequest();
compute_residuals();

vector<double> neighbours_flex(HOURS, 0.0);
double total_deficit_before = 0.0;

  // Calcola il deficit totale prima di qualsiasi shift
for(int h = 0; h < HOURS; h++){
  if(_residuals[h] < 0) total_deficit_before -= _residuals[h];
}


 //INIZIO CICLO DI CONTROLLO PER OGNI ORA
for(int h = 0; h < HOURS; h++){

  // Se il margine è negativo, cerco di shiftare
    if(_residuals[h] < 0){

        double max_flex = numeric_limits<double>::lowest();

        for(const auto& [name,Strategy] : _neighbours){

          if(Strategy._flex[h] > max_flex){
            max_flex = Strategy._flex[h];
          }
        }
        neighbours_flex[h] = max_flex;
      


    // CONTROLLO SE SONO IL PIU FLESSIBILE
      if(_strategy._flex[h] > neighbours_flex[h] ){
          
        // funzione di shifting
        double full_request = _strategy._requests[h];  
        double window = _strategy._flex[h];           
        int start = std::max(0, h - (int)window);
        int end = std::min(HOURS, h + (int)window + 1);
      
        // Trova MIGLIORE ora nella finestra (residual PIÙ positivo)
        int best_hour = h;
        double best_score = -1e9;

        // check the highest residual slot
        for(int nh = start; nh < end; nh++){
          double score = _residuals[nh]; 
          if(score > best_score){
              best_score = score;
              best_hour = nh;
          }
        }
      
        // ALL-OR-NOTHING: sposta tutta la task in uno slot migliore
        if(best_hour != h){  // Solo se diverso dall'originale
            _strategy._requests[best_hour] += full_request;
            _strategy._requests[h] -= full_request;  // SPEGNE qui
        }
        
        _strategy._last_active = steady_clock::now();

        double total_deficit_now = 0.0;
        for(int i = 0; i < HOURS; i++) {
            if(_residuals[i] < 0){
              total_deficit_now -= _residuals[i];
            }
        }
        
        // CONTROLLO DI CONVEREGENZA: se il deficit totale non è migliorato di almeno il 5%
        if(total_deficit_now > total_deficit_before * 0.95 || total_deficit_now < 10.0) {
            break;  // Esce dal ciclo for(h)!
        }
      }
    }

  }
}

double Socialist::get_current_request(){

  lock_guard<mutex> lock(_data_mutex);

  auto now_time_t = chrono::system_clock::to_time_t(chrono::system_clock::now());
  tm local_tm;
  localtime_r(&now_time_t, &local_tm);
  int current_hour = local_tm.tm_hour;

  return _strategy._requests[current_hour];  
}


void Socialist::add_noise(){

  for(int i = 0; i < HOURS; i++){
    _strategy._flex[i] += _dis(_gen);
  }
  
}



/*
  _____ _   _ ___ 
 |_   _| | | |_ _|
   | | | | | || | 
   | | | |_| || | 
   |_|  \___/|___|
                  
*/



void Socialist::run_planner_ui(std::atomic<bool>& global_running) {
    using namespace ftxui;

    if (_strategy._requests.size() != 24) _strategy._requests.resize(24, 0.0);
    if (_strategy._flex.size() != 24) _strategy._flex.resize(24, 0.0);
    
    if (_is_manual.size() != 24) _is_manual.assign(24, false);

    auto screen = ScreenInteractive::Fullscreen();
    int cursor = 0;
    
    std::string input_buffer;
    bool is_editing_power = false;
    bool is_editing_flex = false;

    auto renderer = Renderer([&] {
      auto header_legend = hbox({
      filler(),
      hbox({
          text(" HOUR ") | size(WIDTH, EQUAL, 6) | center | border,
          separator(),
          text(" POW ") | size(WIDTH, EQUAL, 10) | center | border,
          separator(),
          text(" FLEX ") | size(WIDTH, EQUAL, 10) | center | border,
        }) | bold | color(Color::Yellow),
        filler(),
      });

      auto make_row = [&](int i) {
          bool is_selected = (i == cursor);
          auto manual_style = _is_manual[i] ? color(Color::Yellow) : nothing;
          auto base_style = is_selected ? (bgcolor(Color::Blue) | bold | color(Color::White)) : manual_style;
          
          return hbox({
              text(std::to_string(i) + ":00") | size(WIDTH, EQUAL, 6) | center | border,
              text(std::to_string((int)_strategy._requests[i])) | size(WIDTH, EQUAL, 10) | center | border | base_style,
              text(std::to_string((int)_strategy._flex[i])) | size(WIDTH, EQUAL, 10) | center | border | base_style,
          });
      };

      Elements left_col, right_col;
      for (int i = 0; i < 12; ++i) left_col.push_back(make_row(i));
      for (int i = 12; i < 24; ++i) right_col.push_back(make_row(i));

      auto table_layout = hbox({
          filler(),
          vbox(std::move(left_col)),
          separator(),
          vbox(std::move(right_col)),
          filler(),
      });

        Element footer;
        if (is_editing_power || is_editing_flex) {
            std::string pmt = is_editing_power ? " EDIT POWER [" : " EDIT FLEX [";
            pmt += std::to_string(cursor) + ":00]: ";
            footer = hbox({
                text(pmt) | bold | color(Color::Cyan),
                text(input_buffer) | color(Color::White) | inverted | blink,
                ftxui::filler()
            }) | border;
        } else {
            footer = hbox({
                text(" [ARROWS]: Move | [E]: Power | [F]: Flex | [Q]: Quit "),
                ftxui::filler(),
                text(global_running ? " GRID CONNECTED " : " OFFLINE ") 
                    | bgcolor(Color::Green) | color(Color::Black)
            }) | border;
        }

        return vbox({
        text(" PLANNER INTERFACE ") | center | bold | borderDouble, header_legend, table_layout | flex, footer
        }) | border;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        
        if (is_editing_power || is_editing_flex) {
            if (event == Event::Return) {
                double val = 0.0;
                
                // Logica di acquisizione valore
                if (input_buffer.empty() && cursor > 0) {
                    val = is_editing_power ? _strategy._requests[cursor-1] : _strategy._flex[cursor-1];
                } else if (!input_buffer.empty()) {
                    try { val = std::stod(input_buffer); } catch(...) { return true; }
                }

                lock_guard<mutex> lock(_data_mutex);

                if (is_editing_power) _strategy._requests[cursor] = val;
                else _strategy._flex[cursor] = val;
                
                _is_manual[cursor] = true; 
                _noise = false; 
                for (int i = cursor + 1; i < 24; ++i) {
                    if (_is_manual[i]) break; 
                    if (is_editing_power) _strategy._requests[i] = val;
                    else _strategy._flex[i] = val;
                }

                is_editing_power = is_editing_flex = false;
                input_buffer = "";
                return true;
            }
            if (event == Event::Escape) { is_editing_power = is_editing_flex = false; return true; }
            if (event == Event::Backspace) { if(!input_buffer.empty()) input_buffer.pop_back(); return true; }
            if (event.is_character()) {
                char c = event.character()[0];
                if (isdigit(c) || c == '.') { input_buffer += c; return true; }
            }
            return true; 
        }

        if (event == Event::Character('q')) { 
            global_running = false; 
            screen.ExitLoopClosure()(); 
            return true; 
        }
        if (event == Event::ArrowUp)    { cursor = std::max(0, cursor - 1); return true; }
        if (event == Event::ArrowDown)  { cursor = std::min(23, cursor + 1); return true; }
        if (event == Event::ArrowRight) { if (cursor < 12) cursor = std::min(23, cursor + 12); return true; }
        if (event == Event::ArrowLeft)  { if (cursor >= 12) cursor -= 12; return true; }
        
        if (event == Event::Character('e')) { is_editing_power = true; input_buffer = ""; return true; }
        if (event == Event::Character('f')) { is_editing_flex = true; input_buffer = ""; return true; }

        return false;
    });

    screen.Loop(component);

    if(!_noise){
      _noise = true;
      add_noise(); 
    }
}