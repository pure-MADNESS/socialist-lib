/*

  ____             _       _ _     _          _               
 / ___|  ___   ___(_) __ _| (_)___| |_    ___| | __ _ ___ ___ 
 \___ \ / _ \ / __| |/ _` | | / __| __|  / __| |/ _` / __/ __|
  ___) | (_) | (__| | (_| | | \__ \ |_  | (__| | (_| \__ \__ \
 |____/ \___/ \___|_|\__,_|_|_|___/\__|  \___|_|\__,_|___/___/
                                                              

*/

#include "socialist.hpp"

void Socialist::listen(json const &input, string topic) {
  lock_guard<mutex> lock(_data_mutex);

  try {
    if (topic.rfind("source", 0) == 0) {
      if (!input.contains("hourly")) return;

      auto& source = _powers[topic]; 
      
      if (source._powers.size() != HOURS) {
        source._powers.assign(HOURS, 0.0);
      }

      auto const& json_array = input.at("hourly");
      for (int i = 0; i < HOURS && i < (int)json_array.size(); ++i) {
        source._powers[i] = json_array.at(i).get<double>();
      }
      source._last_active = steady_clock::now();

    } else if (topic.rfind("load", 0) == 0) {
      if (!input.contains("hourly")) return;

      auto& neighbour = _neighbours[topic];

      if (neighbour._requests.size() != HOURS) neighbour._requests.assign(HOURS, 0.0);
      if (neighbour._flex.size() != HOURS) neighbour._flex.assign(HOURS, 0.0);

      auto const& hourly_obj = input.at("hourly");

      if (hourly_obj.contains("requests") && hourly_obj.contains("flexibilities")) {
        auto const& req_arr = hourly_obj.at("requests");
        auto const& flex_arr = hourly_obj.at("flexibilities");

        for (int i = 0; i < HOURS && i < (int)req_arr.size(); i++) {
          neighbour._requests[i] = req_arr.at(i).get<double>();
          neighbour._flex[i] = flex_arr.at(i).get<double>();
        }
      }
      neighbour._last_active = steady_clock::now();

    }
  } catch (const std::exception& e) {
    std::cerr << "[Socialist] JSON parsing error on topic " << topic << ": " << e.what() << std::endl;
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


void Socialist::update_strategy() {

  lock_guard<mutex> lock(_data_mutex);

  pop_totalPowers(); 
  pop_totalRequest(); 
  compute_residuals();

  auto now_time_t = chrono::system_clock::to_time_t(chrono::system_clock::now());
  tm local_tm;
  localtime_r(&now_time_t, &local_tm);
  int current_hour = local_tm.tm_hour;
  int search_limit = current_hour + 1;

  for (int h = 0; h < HOURS; h++) {
    
    // find block
    int duration = _strategy._durations[h];
    
    if (duration < 1 || _strategy._requests[h] <= 0 || h <= search_limit) {
        continue; 
    }

    // check flexibility = check starting hour h (task of the same hours interval has the same flexibility)
    double my_flex = _strategy._flex[h];
    double max_neighbor_flex = 0;
    for (auto const& [name, s] : _neighbours) {
      // check neighbor flexibility
      for (int k = 0; k < duration && (h + k) < HOURS; k++) {
        max_neighbor_flex = max(max_neighbor_flex, s._flex[h + k]);
      }
    }

    // if the current instance is the more flexible, then find a new slot
    if (my_flex > max_neighbor_flex) {
      int window = (int)my_flex;
      int start_search = max(search_limit + 1, h - window);
      int end_search = min(HOURS - duration, h + window);

      int best_start_hour = h;
      double best_score = -1e9;

      double current_block_score = 0;
      for (int k = 0; k < duration; k++){
        current_block_score += _residuals[h + k];
      }

      best_score = current_block_score;

      // check a valid window/interval that has the better margin
      for (int nh = start_search; nh <= end_search; nh++) {
        double total_nh_score = 0;
        for (int k = 0; k < duration; k++) {
          total_nh_score += _residuals[nh + k];
        }

        if (total_nh_score > best_score) {
          best_score = total_nh_score;
          best_start_hour = nh;
        }
      }

      // shift
      if (best_start_hour != h) {
        double p_val = _strategy._requests[h];
        double f_val = _strategy._flex[h];
        int d_val = _strategy._durations[h];

        for (int k = 0; k < duration; k++) {
          _strategy._requests[h + k] -= p_val;
          _strategy._flex[h + k] = 0;
          _strategy._durations[h + k] = 1;
        }

        for (int k = 0; k < duration; k++) {
          _strategy._requests[best_start_hour + k] = p_val;
          _strategy._flex[best_start_hour + k] = f_val;
          _strategy._durations[best_start_hour + k] = (k == 0) ? d_val : 0;
        }
        
        _strategy._last_active = steady_clock::now();
        h = max(h, best_start_hour + duration - 1); 
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




void Socialist::add_noise() {

  lock_guard<mutex> lock(_data_mutex);

  for (int i = 0; i < HOURS; i++) {
    if (_strategy._durations[i] > 0) {

      double n = _dis(_gen);
      int dur = _strategy._durations[i];

      for (int j = 0; j < dur && (i + j) < HOURS; j++) {
        _strategy._flex[i + j] += n;
      }

      i += (dur - 1);
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

void Socialist::run_planner_ui(atomic<bool>& global_running) {

  using namespace ftxui;
  if (_strategy._requests.size() != 24) _strategy._requests.resize(24, 0.0);
  if (_strategy._flex.size() != 24) _strategy._flex.resize(24, 0.0);
  if (_strategy._durations.size() != 24) _strategy._durations.assign(24, 1);
  if (_is_manual.size() != 24) _is_manual.assign(24, false);

  auto get_now_hour = []() {
    auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
    return localtime(&now)->tm_hour;
  };

  auto screen = ScreenInteractive::Fullscreen();
  int cursor = get_now_hour();

  string input_buffer;

  bool is_editing_power = false;
  bool is_editing_flex = false;
  bool is_editing_dur = false;

  auto renderer = Renderer([&] {

    int current_hour = get_now_hour();
    lock_guard<mutex> lock(_data_mutex);

    auto header_legend = hbox({

      filler(),
      hbox({
        text(" HOUR ") | size(WIDTH, EQUAL, 6) | center | border,
        separator(),
        text(" POW ")  | size(WIDTH, EQUAL, 8) | center | border,
        separator(),
        text(" FLEX ") | size(WIDTH, EQUAL, 8) | center | border,
        separator(),
        text(" DUR ")  | size(WIDTH, EQUAL, 5) | center | border,
      }) | bold | color(Color::Yellow),
      filler(),

    });

    auto make_row = [&](int i) {

      bool is_past = (i < current_hour);
      bool is_selected = (i == cursor && !is_past);
      bool is_part_of_block = (!is_past && _strategy._durations[i] == 0);

      if (is_past) {
        auto past_style = color(Color::GrayDark);
        return hbox({
          text(to_string(i) + ":00") | size(WIDTH, EQUAL, 6) | center | dim,
          separator() | dim,
          text(to_string((int)_strategy._requests[i])) | size(WIDTH, EQUAL, 8) | center | past_style,
          separator() | dim,
          text(to_string((int)_strategy._flex[i])) | size(WIDTH, EQUAL, 8) | center | past_style,
          separator() | dim,
          text("-") | size(WIDTH, EQUAL, 5) | center | past_style,
        }) | border | dim;

      } else {

        auto manual_style = _is_manual[i] ? color(Color::Yellow) : (is_part_of_block ? color(Color::Cyan) : nothing);
        auto base_style = is_selected ? (bgcolor(Color::Blue) | bold | color(Color::White)) : manual_style;
        string dur_display = (_strategy._durations[i] > 0) ? to_string(_strategy._durations[i]) : "»";
        return hbox({
          text(to_string(i) + ":00") | size(WIDTH, EQUAL, 6) | center | border,
          text(to_string((int)_strategy._requests[i])) | size(WIDTH, EQUAL, 8) | center | border | base_style,
          text(to_string((int)_strategy._flex[i])) | size(WIDTH, EQUAL, 8) | center | border | base_style,
          text(dur_display) | size(WIDTH, EQUAL, 5) | center | border | base_style,
        });
      }
    };

    Elements left_col, right_col;

    for (int i = 0; i < 12; ++i) left_col.push_back(make_row(i));
    for (int i = 12; i < 24; ++i) right_col.push_back(make_row(i));
    
    auto table_layout = hbox({
      filler(), vbox(std::move(left_col)), separator(), vbox(std::move(right_col)), filler(),
    });

    Element footer;

    if (is_editing_power || is_editing_flex || is_editing_dur) {

      string pmt = is_editing_power ? " POW " : (is_editing_flex ? " FLEX " : " DUR ");
      footer = hbox({
        text(" EDITING" + pmt + "[" + to_string(cursor) + ":00]: ") | bold | color(Color::Cyan),
        text(input_buffer) | color(Color::White) | inverted | blink,
        ftxui::filler()
      }) | border;

    } else {

      footer = hbox({
        text(" [E]: Pow | [F]: Flex | [D]: Dur | [ARROWS]: Move | [Q]: Quit "),
        ftxui::filler(),
        text(global_running ? " GRID CONNECTED " : " OFFLINE ") | bgcolor(Color::Green) | color(Color::Black)
      }) | border;
    }

    return vbox({
      text(" PLANNER INTERFACE ") | center | bold | borderDouble,
      header_legend,
      table_layout | flex,
      footer
    }) | border;
  });

  auto component = CatchEvent(renderer, [&](Event event) {
    int current_hour = get_now_hour();

    if (is_editing_power || is_editing_flex || is_editing_dur) {

      if (event == Event::Return) {

        double val = 0.0;
        try { 
          val = input_buffer.empty() ? 0.0 : stod(input_buffer);
        } catch(...) { return true; }

        lock_guard<mutex> lock(_data_mutex);

        if (is_editing_dur) {
          int dur = max(1, min(24 - cursor, (int)val));
          _strategy._durations[cursor] = dur;
          for (int j = 1; j < dur; ++j) {
            _strategy._requests[cursor + j] = _strategy._requests[cursor];
            _strategy._flex[cursor + j] = _strategy._flex[cursor];
            _strategy._durations[cursor + j] = 0;
            _is_manual[cursor + j] = true;
          }

        } else {

          if (is_editing_power) _strategy._requests[cursor] = val;
          else _strategy._flex[cursor] = val;
        }

        _is_manual[cursor] = true;
        _noise = false;

        if (!is_editing_dur && _strategy._durations[cursor] <= 1) {

          for (int i = cursor + 1; i < 24; ++i) {
            if (_is_manual[i]) break; 
            if (is_editing_power) _strategy._requests[i] = val;
            else _strategy._flex[i] = val;
          }
        }

        is_editing_power = is_editing_flex = is_editing_dur = false;
        input_buffer = "";
        return true;
      }

      if (event == Event::Escape) { is_editing_power = is_editing_flex = is_editing_dur = false; return true; }
      if (event == Event::Backspace) { if(!input_buffer.empty()) input_buffer.pop_back(); return true; }
      if (event.is_character()) {
        char c = event.character()[0];
        if (isdigit(c) || c == '.') { input_buffer += c; return true; }
      }
      return true; 
    }

    if (event == Event::Character('q')) { global_running = false; screen.ExitLoopClosure()(); return true; }
    if (event == Event::Character('e')) { is_editing_power = true; input_buffer = ""; return true; }
    if (event == Event::Character('f')) { is_editing_flex = true; input_buffer = ""; return true; }
    if (event == Event::Character('d')) { is_editing_dur = true; input_buffer = ""; return true; }
    if (event == Event::ArrowUp)    { cursor = max(current_hour, cursor - 1); return true; }
    if (event == Event::ArrowDown)  { cursor = min(23, cursor + 1); return true; }
    if (event == Event::ArrowRight) { if (cursor < 12) cursor = min(23, max(current_hour, cursor + 12)); return true; }
    if (event == Event::ArrowLeft)  { if (cursor >= 12) cursor = max(current_hour, cursor - 12); return true; }
    return false;
  });

  screen.Loop(component);
  if(!_noise){ _noise = true; add_noise(); }
}