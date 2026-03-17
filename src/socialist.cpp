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

void update_strategy(){


    
}