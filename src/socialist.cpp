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


array<string, HOURS> flex_loads; // array per tenere traccia dei carichi da shiftare
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
        string max_flex_load;

        for(const auto& [name,Strategy] : _neighbours){

          if(Strategy._flex[h] > max_flex){
            max_flex = Strategy._flex[h];
            max_flex_load = name; // non usato, ma potrebbe essere utile per debug o estensioni future
          }
        }
        neighbours_flex[h] = max_flex;
        flex_loads[h] = max_flex_load; // non usato
      


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
          _strategy._requests[h] = 0.0;  // SPEGNE qui
      }
      
      _strategy._last_active = steady_clock::now();

            
      double total_deficit_now = 0.0;
      for(int i = 0; i < HOURS; i++) {
          if(_residuals[i] < 0) total_deficit_now -= _residuals[i];
      }
      
      // CONTROLLO DI CONVEREGENZA: se il deficit totale non è migliorato di almeno il 5%
      if(total_deficit_now > total_deficit_before * 0.95 || total_deficit_now < 10.0) {
          break;  // Esce dal ciclo for(h)!
      }
        }

    }

  }
}




/*
  _____ _   _ ___ 
 |_   _| | | |_ _|
   | | | | | || | 
   | | | |_| || | 
   |_|  \___/|___|
                  
*/

void Socialist::display_tui(const vector<double>& powers, const vector<double>& flex, int cursor){
  
  std::cerr << "\033[H\033[J";
  clear_screen();
  cerr << "=== PLANNER INTERFACE ===" << endl;
  cerr << "Commands: [w] up, [s] down, [e] edit, [q] save and exit" << endl;
  cerr << "NOTE: if the value [e] is empty, the previous value is taken." << endl;
  cerr << "-----------------------------------------------" << endl;
  cerr << "  HOUR   |   POWER (W)   |   FLEXIBILITY'   " << endl;
  cerr << "-----------------------------------------------" << endl;

  for (size_t i = 0; i < powers.size(); ++i) {
      if (i == (size_t)cursor) cerr << " > "; else cerr << "   ";
      cerr << setw(2) << i << ":00 | " 
                << setw(13) << powers[i] << " | " 
                << setw(15) << flex[i] << endl;
  }

}

void Socialist::run_planner_ui(atomic<bool>& global_running) {
    using namespace ftxui;

    // Assicuriamoci che i vettori abbiano la dimensione corretta
    if (_strategy._requests.size() != 24) _strategy._requests.resize(24, 0.0);
    if (_strategy._flex.size() != 24) _strategy._flex.resize(24, 0.0);

    auto screen = ScreenInteractive::Fullscreen();
    int cursor = 0;
    
    // Variabili di stato per l'editing
    std::string input_buffer;
    bool is_editing_power = false;
    bool is_editing_flex = false;

    // Componente di Rendering (Disegna l'interfaccia)
    auto renderer = Renderer([&] {
        Elements rows;
        
        // Header
        rows.push_back(hbox({
            text(" ORA ") | size(WIDTH, EQUAL, 8) | border,
            text(" POTENZA (W) ") | flex | border,
            text(" FLESSIBILITA' ") | flex | border,
        }) | bold | color(Color::Yellow));

        // Tabella
        for (int i = 0; i < 24; ++i) {
            bool is_selected = (i == cursor);
            auto row_style = is_selected ? (bgcolor(Color::Blue) | bold) : nothing;
            
            rows.push_back(hbox({
                text(std::to_string(i) + ":00") | size(WIDTH, EQUAL, 8) | center | border,
                text(std::to_string((int)_strategy._requests[i])) | flex | center | border | row_style,
                text(std::to_string((int)_strategy._flex[i])) | flex | center | border | row_style,
            }));
        }

        // Barra di stato / Input dinamico
        Element footer;
        if (is_editing_power || is_editing_flex) {
            std::string prompt = is_editing_power ? " EDIT POTENZA [H-" + std::to_string(cursor) + "]: " 
                                                 : " EDIT FLEX [H-" + std::to_string(cursor) + "]: ";
            footer = hbox({
                text(prompt) | bold | color(Color::Cyan),
                text(input_buffer) | color(Color::White) | blink,
                ftxui::fill() // Riempie il resto della riga
            }) | border;
        } else {
            footer = hbox({
                text(" [Frecce]: Muovi | [E]: Modifica Potenza | [F]: Modifica Flex | [Q]: Esci "),
                ftxui::fill(),
                text(global_running ? " GRID CONNECTED " : " OFFLINE ") | bgcolor(Color::Green) | color(Color::Black)
            }) | border;
        }

        return vbox({
            text(" SOCIALIST PLANNER INTERFACE ") | center | bold | borderDouble,
            vbox(std::move(rows)) | vscroll_indicator | frame | flex,
            footer
        }) | border;
    });

    // Gestore Eventi (Cattura i tasti)
    auto component = CatchEvent(renderer, [&](Event event) {
        
        // LOGICA DI EDITING (Se stiamo scrivendo un numero)
        if (is_editing_power || is_editing_flex) {
            // Conferma con INVIO
            if (event == Event::Return) {
                if (input_buffer.empty() && cursor > 0) {
                    // Copia valore ora precedente
                    if (is_editing_power) _strategy._requests[cursor] = _strategy._requests[cursor - 1];
                    else _strategy._flex[cursor] = _strategy._flex[cursor - 1];
                } else if (!input_buffer.empty()) {
                    try {
                        if (is_editing_power) _strategy._requests[cursor] = std::stod(input_buffer);
                        else _strategy._flex[cursor] = std::stod(input_buffer);
                    } catch (...) {} // Ignora input non validi
                }
                input_buffer = "";
                is_editing_power = is_editing_flex = false;
                return true;
            }
            
            // Annulla con ESC
            if (event == Event::Escape) {
                input_buffer = "";
                is_editing_power = is_editing_flex = false;
                return true;
            }

            // Backspace per cancellare
            if (event == Event::Backspace) {
                if (!input_buffer.empty()) input_buffer.pop_back();
                return true;
            }

            // Inserimento numeri e punto decimale
            if (event.is_character()) {
                char c = event.character()[0];
                if (std::isdigit(c) || c == '.') {
                    input_buffer += c;
                }
                return true;
            }
            return true; // Blocca altri tasti mentre editi
        }

        // LOGICA DI NAVIGAZIONE (Modo normale)
        if (event == Event::Character('q')) {
            screen.ExitLoopClosure()();
            global_running = false;
            return true;
        }

        if (event == Event::ArrowUp)   { cursor = std::max(0, cursor - 1); return true; }
        if (event == Event::ArrowDown) { cursor = std::min(23, cursor + 1); return true; }
        
        if (event == Event::Character('e')) { is_editing_power = true; input_buffer = ""; return true; }
        if (event == Event::Character('f')) { is_editing_flex = true; input_buffer = ""; return true; }

        return false;
    });

    screen.Loop(component);
}