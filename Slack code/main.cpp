/*
//  main.cpp
//  Slack code
//
//  Created by Julien Brinson on 8/18/16.
//  Copyright © 2016 Julien Brinson. All rights reserved.
//
    This small slack implementation has been very fun to work on. The information
    is modeled using two maps for the channels and messages respectively. The program
    also uses two threads. One thread is for the front end of the program. It works 
    primarily to handle the user's input and changing the display. The other thread
    is for the backend and handles all of the calls to the server and reading/writing
    files. In order to write this program I extensively leveraged the help of two outside libraries
    Jsoncpp and CURL. Both libraries had extensive documentation from users and was compatible with 
    most platforms/OS. Although I did have some difficulties with them, I was able to overcome them.
 */

#include <iostream>
#include <string>
#include <curl/curl.h>  //communicating with the server
#include "json/json.h" //manipulating Json
#include <fstream>
#include <deque>
#include <stdio.h>
#include <time.h>
#include <algorithm>
#include <thread>
#include <condition_variable>
#include <chrono>


const std::string APIURL = "https://slack.com/api/";
std::deque<bool> finished;
std::condition_variable back_end, front_end;
std::mutex cv_m, cv_m2, file_lock, map_lock, output_lock, queue_lock;
std::chrono::high_resolution_clock::time_point last_alert = std::chrono::high_resolution_clock::now();


/* retrieve_data_callback: this the callback used by CURL to store the data that 
 comes back from the Slack server. 
 */

size_t retrieve_data_callback(char *data, size_t size, size_t nmemb, void *raw_json){
    ((std::string *)raw_json)->append(data, size*nmemb);
    return size*nmemb;
}

/* error_delay(): This little method helps to space out some of the error messages that the output isn't 
 completely clogged when the people. It returns true after the specified time_span, else false */
bool error_delay(int time_span = 30){
    std::chrono::duration <double> delay = std::chrono::duration_cast<std::chrono::duration<double>>(last_alert - std::chrono::high_resolution_clock::now());
    if(delay >= std::chrono::seconds(time_span)){
        last_alert = std::chrono::high_resolution_clock::now();
        return true;
    }
    return false;
}

/* parse_payload: this method takes the raw Json input from the slack server and parses it
 uses Jsoncpp into a Json::Value. 
 */
Json::Value parse_payload(std::string &payload){
    Json::Value jObj;
    Json::Reader reader;
    bool parsingSuccessful = reader.parse(payload, jObj);
    if(!parsingSuccessful && error_delay()){
        
        output_lock.lock();
        std::cout<< "failed to parse" << reader.getFormatedErrorMessages();
        output_lock.unlock();
    }
    return jObj;
}

/* edit_map: this method changes a string-to-Json map with a new value into an already existing map
 */
bool edit_map(std::string key, Json::Value new_value, std::map<std::string, Json::Value> *j_objMap){
    map_lock.lock();
    Json::Value old_value;
    try{
        old_value = j_objMap->at(key);
    }catch(const std::out_of_range &oor){
        //map didin't already contain key
        (*j_objMap)[key] = new_value;
        map_lock.unlock();
        return true;
    }
    //map did contain key
    std::pair<std::map<std::string, Json::Value>::iterator, bool> i_Map;
    i_Map = j_objMap->insert(std::pair<std::string, Json::Value>(key, new_value));
    i_Map.first->second = new_value;
    map_lock.unlock();
    if(new_value == old_value){
        return false;
    }else{
        return true;
    }
}

/*build_cacheMap: turns the stored Json cache file into a map. argument content should be a string
 of the type of field used (i.e. "messages" or "channels"). the file expects the cache to be saved 
 in the form "cache_"+content+".txt" (i.e. "cache_messages.txt" or "cache_channes.txt"). This method
 also uses mutexes to ensure that all reading of the file and building the cache are thread safe.
 Because of problems with Json::reader, this method uses a custom read procedure. The method 
 continually gets lines from a file. the data is arranged in the following format:
 
 MAP_KEY:
 {[JSON OBJECT]}
 *break*
 MAP_KEY2:
 {[JSON_OBJECT_2 etc]}
 "
 */
void build_cacheMap(const std::string content, std::map<std::string, Json::Value> *cacheMap){
    std::string file_path = "cache_"+ content + ".txt";
    Json::Value cached_data;
    file_lock.lock();
    std::fstream cache;
    cache.open(file_path, std::fstream::in);
    std::string old_key;
    std::string file_value;
    std::string raw_json;
    std::getline(cache, old_key);
    while(std::getline(cache, file_value)){
        if(!(file_value.compare("*break*")==0)){
            raw_json += file_value;
        }else{
            old_key.pop_back();
            cached_data = parse_payload(raw_json);
            edit_map(old_key, cached_data, cacheMap);
            raw_json.clear();
            std::getline(cache, old_key);
        }
    }
    cache.close();
    file_lock.unlock();
}

/* write_cache: this method writes the cache file from a map of strings-to-Json values. Because 
 of troubles with Json::Reader, a custom read algorith is used. The method writes to the cache file
 using the same convention as mentioned in build_cacheMap. This program also uses 
 mutexes to make sure that all writing to the files are thread safe"
 */
void write_cache(const std::string content, std::map<std::string, Json::Value> *j_objMap){
    file_lock.lock();
    std::string file_path = "cache_"+ content + ".txt";
    std::fstream cache;
    cache.open(file_path, std::fstream::out|std::fstream::trunc);
    for(std::map<std::string, Json::Value>::iterator it = j_objMap->begin(); it != j_objMap->end(); ++it){
        std::string map_payload = it->first + ":\n"+ it->second.toStyledString() + "\n*break*\n";
        cache.write(map_payload.c_str(), map_payload.length());
    }
    cache.close();
    file_lock.unlock();
}



/* call_slack: This method constructs the URL needed to communicate with the Slack server. Payload is to be a
 string passed by reference to represent the server response. This method displays an error message
 if you are no longer connected to the internet
 */
void call_slack(std::string slack_token, std::string methodName, std::string &payload, std::string param, std::map<std::string, Json::Value> *j_objMap){
    CURL *curl;
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, retrieve_data_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &payload);
    std::string request_url = APIURL + methodName +"?token=" + slack_token + param;
    curl_easy_setopt(curl, CURLOPT_URL, request_url.c_str());
    int res;
    if(curl){
        res = curl_easy_perform(curl);
    }
    if(res != CURLE_OK){
        if(error_delay()){
            output_lock.lock();
            std::cout << "The internet connection failed, but you're still able to access all of the information since the last successful connection.\n ";
            output_lock.unlock();
        }
        
    }else{
        //back_end.notify_all();
    }
    curl_easy_cleanup(curl);
}

/*extract_channels: this method loops over the keys in j_channelMap to construct a vector of class names
 */
std::vector<std::string> extract_channels(std::map<std::string, Json::Value> &j_channelMap){
    std::vector<std::string> channels;
    for(std::map<std::string, Json::Value>::iterator i_Map = j_channelMap.begin(); i_Map!= j_channelMap.end(); ++i_Map){
            channels.push_back(i_Map->first);
    }
    return channels;
}

/* decipher_channels: this method converts a raw Json payload into value to be put into the j_channelMap
 */
void decipher_channels(std::string payload, std::map<std::string, Json::Value> *j_channelMap){
    Json::Value jObj = parse_payload(payload);
    Json::Value j_channelList = jObj.get("channels", "default");
    std::vector<std::string> channels;
    int channel_index = 0;
    const char* channel_name = "name";
    while(j_channelList.isValidIndex(channel_index)){
        channels.push_back(j_channelList[channel_index][channel_name].asString());
        edit_map(channels[channel_index], j_channelList[channel_index], j_channelMap);
        channel_index++;
    }
}

/*cache_channel_list: this calls the channels.list slack web API and inserts the result into the j_channelMap
 each key is a string with the channel's name */
void cache_channel_list(std::string slack_token, std::map<std::string, Json::Value> *j_channelMap){
    std::string payload = "";
    std::string methodName = "channels.list";
    std::string param = "";
    call_slack(slack_token, methodName, payload, param, j_channelMap);
    if(payload.length() != 0){
        decipher_channels(payload, j_channelMap);
    }
    
}

/*display_channel_list: this method uses the j_channelMap to display the channel list to the user;
 It also uses mutex to make sure the output is thread safe */
std::vector<std::string> display_channel_list(std::string slack_token, std::map<std::string, Json::Value> &j_channelMap){
    std::vector<std::string> channels = extract_channels(j_channelMap);
    output_lock.lock();
    std::cout << "Channel List:";
    for(int channel_index = 0; channel_index<channels.size(); channel_index++){
        std::cout <<  "\n" << channel_index+1 << ":" << channels[channel_index];
    }
    std::cout << "\n";
    output_lock.unlock();
    return channels;
    
}

/*isMessage: this method identifies whether or not a Json object has a message type and returns bool with the answer */
bool isMessage(const Json::Value j_message){
    std::string message = "message";
    return message.compare(j_message["type"].asString()) == 0;
}

/* extract_text: this method returns a string with the text field of a Json message object */
std::string extract_text(Json::Value j_message){
    const char* text = "text";
    return j_message[text].asString();
}

/* recover_id: this method recovers the channel_id from the list of parameters used to 
 construct a SLACK URL. This method assumes that "&channel=" is the first element in the string
 */
std::string recover_id(std::string param){
    const char *input = param.c_str();
    size_t total_length = strcspn(input+1, "&")+1;
    size_t unwanted_length = strcspn(input, "=")+1;
    size_t final_length = total_length - unwanted_length;
    char channel_id[10] = {};
    strncpy(channel_id, input+unwanted_length, final_length);
    return std::string(channel_id);
}

/*update_paramter: this method changes the parameters for a slack URL so that more of a channel history can be 
 discovered using the oldest 'ts' from the most recent call to channels.history */
std::string update_parameter(std::string param, const Json::Value jObj){
    while(param.back() != '='){
        param.pop_back();
    }
    param += jObj[jObj.size()-1]["ts"].asString();
    return param;
}

/*get_message_batch: this method retrieves an entire channel's history by recursively calling the slack server
 when "has more"= true. the results are stored in the j_messageList object passed in by reference
 in the event there is no internet connection, the method uses the information stored in j_messageMap
 */
void get_message_batch(std::string slack_token, std::string param, Json::Value &j_messageList, std::map<std::string, Json::Value> *j_messageMap){
    std::string payload;
    call_slack(slack_token, "channels.history", payload, param, j_messageMap);
    std::string channel_id = recover_id(param);
    const Json::Value j_Obj = parse_payload(payload);
    if(payload.length() == 0){ //use cache data
        try{
            j_messageList = j_messageMap->at(channel_id);
        }catch(const std::out_of_range &oor){
            output_lock.lock();
            std::cout<< "unkown channel id given: "+ channel_id + "\n";
            output_lock.unlock();
        }
    }else{
        if(j_Obj["has_more"].asBool()){
            std::string new_param = update_parameter(param, j_Obj["messages"]);
            get_message_batch(slack_token, new_param, j_messageList, j_messageMap);
        }
        if(j_messageList == (*j_messageMap)[channel_id] && j_messageList.isNull() ){
            Json::Value message_array = j_Obj.get("messages", "default");
            for(int i = 0; i<message_array.size(); i++ ){
                j_messageList.append(message_array[i]);
            }
        }
    }
}

/* compare_time: this function is used to compare time stamps ('ts') of messages for sorting purposes */
bool compare_time(const Json::Value *ptr1, const Json::Value *ptr2){
    double time1 = atof((*ptr1)["ts"].asCString());
    double time2 = atof((*ptr2)["ts"].asCString());
    return time1 < time2;
}

/*extract_messages: this method returns a sorted vector of strings of the messageList. the first index shall be
 the oldest method */
std::vector<std::string> extract_messages(const Json::Value &j_messageList){
    std::vector<const Json::Value*> order;
    for(int i = 0; i< j_messageList.size(); i++){
        const Json::Value *ptr = &j_messageList[i];
        order.push_back(ptr);
    }
    sort(order.begin(), order.end(), compare_time);
    std::vector<std::string> messages;
    for(int i = 0; i < order.size(); i++){
        messages.push_back((*order[i])["text"].asString());
    }
    return messages;
}

/*get_channel_id: this method returns a string representing the unique channel_id for a particular channel
 Json object*/
std::string get_channel_id(Json::Value j_channel){
    std::string channel_id = "";
    const char* id = "id";
    channel_id = j_channel[id].asString();
    return channel_id;
}

/*add_messageMap: this method adds a method channel list to the j_messageMap*/
void add_messageMap(std::string slack_token, Json::Value j_channel, std::map<std::string, Json::Value> *j_messageMap){
    std::string channel_id = j_channel["id"].asString();
    std::string param = "&channel=" + channel_id + "&latest=";
    Json::Value j_messageList;
    get_message_batch(slack_token, param, j_messageList, j_messageMap);
    if(!j_messageList.isNull()){
        edit_map(channel_id, j_messageList, j_messageMap);
    }
}

/*show_message_history: this method displays the message history list to the user.  */
void show_message_history( Json::Value j_channel, std::map<std::string, Json::Value>& j_messageMap){
    Json::Value j_messageList = j_messageMap[j_channel["id"].asString()];
    std::vector<std::string> messages = extract_messages(j_messageList);
    output_lock.lock();
    
    std::cout << j_channel["name"].asString()<<" - Message History: \n";
    for(size_t message_index = 0; message_index < messages.size(); message_index++){
        std::cout << messages[message_index] << "\n";
    }
    
    output_lock.unlock();

}

/* cache_upkeep: this method calls the server to reconstruct the map representing the slack team and subsequently
 writes the data to two cache_files (title "cache_channels.txt" and "cache_messages.txt" respectively);
 */
void cache_upkeep(const std::string slack_token, std::map<std::string, Json::Value> *j_channelMap, std::map<std::string, Json::Value> *j_messageMap){
    cache_channel_list(slack_token, j_channelMap);
    for(std::map<std::string, Json::Value>::iterator it = j_channelMap->begin(); it != j_channelMap->end(); ++it){
        add_messageMap(slack_token, it->second, j_messageMap);
    }
    front_end.notify_all();
    write_cache("channels", j_channelMap);
    write_cache("messages", j_messageMap);
}

/*init: this method sets up the map representing the slack team from the last available data in the people */
void init(std::map<std::string, Json::Value> *j_channelMap, std::map<std::string, Json::Value> *j_messageMap){
    build_cacheMap("channels", j_channelMap);
    build_cacheMap("messages", j_messageMap);
}

/*run_background: this method is the primary background thread for changing the information to represent the slack
 team. The method initializes two maps from the cached data and updates the information from server calls.
 Most server calls are generated when the user interacts with the team, but the method will also periodically 
 call the server to make sure all of the information is up to date */
void run_background(std::map<std::string, Json::Value> *j_channelMap, std::map<std::string, Json::Value>* j_messageMap, std::string *slack_token){
    init(j_channelMap, j_messageMap);
    std::unique_lock<std::mutex> u_lock(cv_m);
    back_end.wait(u_lock);
    cache_upkeep(*slack_token, j_channelMap, j_messageMap);
    while(finished.empty()){
        back_end.wait_for(u_lock, std::chrono::seconds(30));
        output_lock.lock();
        std::cout << "\nBackground checking Slack server\n";
        output_lock.unlock();
        cache_upkeep(*slack_token, j_channelMap, j_messageMap);
        
    }
}

/* want_history: This methods handles the logic of display for the user. taking the input from the user, 
 the function returns true if the user has input a valid channel and false otherwise. The user's desired
 channel is returned by reference in the display_channel parameter */
bool want_history(std::string input, Json::Value & display_channel, std::vector<std::string> channelList, std::map<std::string, Json::Value> &j_channelMap){
    if(isdigit(input[0])){
        int num;
        std::stringstream convert(input);
        if(!(convert>> num)){
            num = 1;
        }
        num--;
        display_channel = j_channelMap[channelList[num]];
        return true;
    }else if(input[0] == 'q' || input[0] == 'u'){ //quit command
        return false;
    }else{
        try{
            display_channel = j_channelMap.at(input);
            return true;
        }catch(const std::out_of_range &oor){
            return false;
        }
    }
}

/*execute_slack: this is the main method that handles displaying the correct information to the user.
 When the user makes gives a command to update, this method signals to background thread to make 
 another call to the server to make sure everything is good! */
void execute_slack(std::string slack_token, std::map<std::string, Json::Value> &j_channelMap, std::map<std::string,Json::Value> &j_messageMap){
    std::unique_lock<std::mutex> lk(cv_m2);
    std::string command = "go";
    Json::Value display_channel;
    while(command[0] != 'q'){
        std::vector<std::string> channelList = display_channel_list(slack_token, j_channelMap);
        std::string channel;
        output_lock.lock();
        std::cout << "commands:\nu - update\n q - quit \n select channel (number or name) for message history: ";
        
        std::cin >> command;
        output_lock.unlock();
        if(command[0] == 'u'){
            back_end.notify_all();
            front_end.wait(lk);
        }
        if(want_history(command, display_channel, channelList, j_channelMap)){
            do{
                
                show_message_history(display_channel, j_messageMap);
                output_lock.lock();
                std::cout << "next commands:\n update: u or update\n Quit: quit or q\n Select new channel: channels or c\n input command: ";
                
                std::cin >> command;
                output_lock.unlock();
                if(command[0] == 'u'){
                    back_end.notify_all();
                    front_end.wait(lk);
                }
            }while(command[0] == 'u');
        }
    }
}


int main(int argc, const char * argv[]) {
    std::map<std::string, Json::Value> j_channelMap;
    std::map<std::string, Json::Value> j_messageMap;
    std::string slack_token;
    std::thread background(run_background, &j_channelMap, &j_messageMap, &slack_token);
    std::cout << "Welcome to Slack!\n";
    std::cout << "Please provide token: ";
    std::cin >> slack_token;
    back_end.notify_all();
    execute_slack(slack_token, j_channelMap, j_messageMap);
    
        finished.push_back(true);
    background.join();
    return 0;
}
