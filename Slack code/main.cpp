//
//  main.cpp
//  Slack code
//
//  Created by Julien Brinson on 8/18/16.
//  Copyright © 2016 Julien Brinson. All rights reserved.
//

#include <iostream>
#include <string>
#include <curl/curl.h>
#include "json/json.h"
#include <pthread.h>
#include <fstream>
#include <queue>
#include <stdio.h>
#include <semaphore.h>

const std::string APIURL = "https://slack.com/api/";
const int CHANNEL_NAME_MAX_LENGTH = 21;
pthread_mutex_t  file_lock, map_lock, output_lock, queue_lock;
const std::string MESSAGES = "message";
const std::string CHANNELS = "channels";
std::queue<bool> info_updated;

size_t retrieve_data_callback(char *data, size_t size, size_t nmemb, void *raw_json){
    ((std::string *)raw_json)->append(data, size*nmemb);
    return size*nmemb;
}

Json::Value parse_payload(std::string &payload){
    Json::Value jObj;
    Json::Reader reader;
    bool parsingSuccessful = reader.parse(payload, jObj);
    if(!parsingSuccessful){
        std::cout<< "failed to parse" << reader.getFormatedErrorMessages();
    }
    return jObj;
}

bool edit_map(std::string key, Json::Value new_value, std::map<std::string, Json::Value> &j_objMap){
    pthread_mutex_lock(&map_lock);
    Json::Value old_value;
    
    try{
        old_value = j_objMap.at(key);
//        pthread_mutex_lock(&output_lock);
//        std::cout << "key = " << key <<"\n element: " << old_value.toStyledString() << "\n";
//        
//        pthread_mutex_unlock(&output_lock);
    }catch(const std::out_of_range &oor){
        //map didin't already contain key
        j_objMap[key] = new_value;
        pthread_mutex_unlock(&map_lock);
//        pthread_mutex_lock(&output_lock);
//        std::cout << "key = " << key <<"\n element: " << new_value.toStyledString() << "\n";
//        pthread_mutex_unlock(&output_lock);
        pthread_mutex_lock(&queue_lock);
        info_updated.push(true);
        pthread_mutex_unlock(&queue_lock);
        return true;
    }
    //map did contain key
    std::pair<std::map<std::string, Json::Value>::iterator, bool> i_Map;
    i_Map = j_objMap.insert(std::pair<std::string, Json::Value>(key, new_value));
    i_Map.first->second = new_value;
    pthread_mutex_unlock(&map_lock);

    if(new_value == old_value){
        //std::cout << "new value ==  old value\n";
        return false;
    }else{
        //return true if the information has been updated
        pthread_mutex_lock(&output_lock);
        std::cout << "old value was: " << old_value.toStyledString() << "\n";
        std::cout << "new value is: " << j_objMap[key].toStyledString() << "\n";
        pthread_mutex_unlock(&output_lock);
        pthread_mutex_lock(&queue_lock);
        info_updated.push(true);
        pthread_mutex_unlock(&queue_lock);
        return true;
    }
}


void build_cacheMap(const std::string content, std::map<std::string, Json::Value> & cacheMap){
    std::string file_path = "cache_"+ content + ".txt";
    Json::Value cached_data;
    pthread_mutex_lock(&file_lock);
    std::fstream cache, test;
    cache.open(file_path, std::fstream::in);
    test.open("test_"+content+ ".txt", std::fstream::out|std::fstream::trunc);
    
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
            test.write(cached_data.toStyledString().c_str(), cached_data.toStyledString().length());
            //pthread_mutex_lock(&map_lock);
            edit_map(old_key, cached_data, cacheMap);
//            cacheMap.insert(std::pair<std::string, Json::Value>(old_key, cached_data));
//            pthread_mutex_unlock(&map_lock);
            raw_json.clear();
            std::getline(cache, old_key);
        }
        
    }
    test.close();
    cache.close();
    pthread_mutex_unlock(&file_lock);
}

void write_cache(const std::string content, std::map<std::string, Json::Value> &j_objMap){
    pthread_mutex_lock(&file_lock);
    std::string file_path = "cache_"+ content + ".txt";
    std::fstream cache;
    cache.open(file_path, std::fstream::out|std::fstream::trunc);
    for(std::map<std::string, Json::Value>::iterator it = j_objMap.begin(); it != j_objMap.end(); ++it){
        std::string map_payload = it->first + ":\n"+ it->second.toStyledString() + "\n*break*\n";
        cache.write(map_payload.c_str(), map_payload.length());
        
    }
    cache.close();
    pthread_mutex_unlock(&file_lock);
}

void call_slack(std::string slack_token, std::string methodName, std::string &payload, std::string param, std::map<std::string, Json::Value> &j_objMap){
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
        pthread_mutex_lock(&output_lock);
        std::cout << "The internet connection failed, but you're still able to access all of the information since the last successful connection. ";
        pthread_mutex_unlock(&output_lock);
//        pthread_mutex_lock(&queue_lock);
//        if(info_updated.empty() && j_objMap.size()==0){//init cacheMap
//            pthread_mutex_unlock(&queue_lock);
//            if(methodName.compare("channels.list")==0){
//                build_cacheMap("channels", j_objMap);
//            }else{
//                build_cacheMap("messages", j_objMap);
//            }
//        }
//        else if(!info_updated.empty()){
//            info_updated.pop();
//            pthread_mutex_unlock(&queue_lock);
//            if(methodName.compare("channels.list")==0){
//                write_cache("channels", j_objMap);
//            }else{
//                write_cache("messages", j_objMap);
//            }
//        }
    }else{
        info_updated.push(true);
        pthread_mutex_unlock(&queue_lock);
    }
    
    curl_easy_cleanup(curl);
}



std::vector<std::string> extract_channels(std::string &payload, std::map<std::string, Json::Value> &j_channelMap){
    std::vector<std::string> channels;
    if(payload.length()==0 && j_channelMap.size() > 0){//cached data!
        for(std::map<std::string, Json::Value>::iterator i_Map = j_channelMap.begin(); i_Map!= j_channelMap.end(); ++i_Map){
            channels.push_back(i_Map->first);
        }
    }else{
        Json::Value jObj = parse_payload(payload);
        Json::Value j_channelList = jObj.get("channels", "default");
        
        int channel_index = 0;
        //const char* channel_name = "name";
        while(j_channelList.isValidIndex(channel_index)){
            
            std::string name = j_channelList[channel_index]["name"].asString();
            edit_map(name, j_channelList[channel_index], j_channelMap);
            
            channels.push_back(name);
            channel_index++;
        }
//        while(j_channelList.isValidIndex(channel_index)){
//            j_channelMap
//            channels.push_back(j_channelList[channel_index][channel_name].asString());
//            pthread_mutex_lock(&map_lock);
//            j_channelMap.insert(std::pair<std::string, Json::Value>(channels[channel_index], j_channelList[channel_index]));
//            pthread_mutex_unlock(&map_lock);
//            channel_index++;
//        }
    }
    return channels;
}




bool test_map(std::map<std::string, Json::Value> &cacheMap, std::map<std::string, Json::Value> &j_objMap){
    if(cacheMap.size() != j_objMap.size()){ //cache is not the same size, so cache needs to be update
        return false;
    }
    for(std::map<std::string, Json::Value>::iterator it = cacheMap.begin(); it != cacheMap.end(); ++it){
        try{
            if(j_objMap.at(it->first) != it->second || (j_objMap.at(it->first).toStyledString().compare(it->second.toStyledString()) == 0)){ //info has been updated
                return false;
            }
        }catch(const std::out_of_range &oor){
            std::cout << "Some cache data has been deleted because it is no longer relevant\n";
            return false;
        }
    }
    return true;
}



void cache_upkeep(const std::string content, std::map<std::string, Json::Value> &j_objMap){
    std::map<std::string, Json::Value> cacheMap;
    pthread_mutex_lock(&queue_lock);
    if(!info_updated.empty()){
        while(!info_updated.empty()){ //get rid of excess update request for just one update of cache
            info_updated.pop();
        }
        pthread_mutex_unlock(&queue_lock);
        //build_cacheMap(content, cacheMap);
//        if(!test_map(cacheMap, j_objMap)){
        write_cache(content, j_objMap);
        //}
    }
}


std::vector<std::string> cache_channel_list(std::string slack_token, std::map<std::string, Json::Value> &j_channelMap){
    std::string payload = "";
    std::string methodName = "channels.list";
    std::string param = "";
    call_slack(slack_token, methodName, payload, param, j_channelMap);
    std::vector<std::string> channels = extract_channels(payload, j_channelMap);
    cache_upkeep("channels", j_channelMap);
    
    return channels;
    
}

std::vector<std::string> get_channel_list(std::string slack_token, std::map<std::string, Json::Value> &j_channelMap){
    std::vector<std::string> channels = cache_channel_list(slack_token, j_channelMap);
    pthread_mutex_unlock(&output_lock);
    std::cout << "Channel List:";
    for(int channel_index = 0; channel_index<channels.size(); channel_index++){
        std::cout <<  "\n" << channel_index+1 << ":" << channels[channel_index];
    }
    std::cout << "\n";
    pthread_mutex_unlock(&output_lock);
    return channels;
}

bool isMessage(const Json::Value j_message){
    const char* type = "type";
    std::string message = "message";
    return message.compare(j_message[type].asString()) == 0;
}

std::string extract_text(Json::Value j_message){
    const char* text = "text";
    return j_message[text].asString();
}

std::vector<std::string> extract_messages(std::string &payload, std::string channel_id, std::map<std::string, Json::Value> &j_messageMap){
    std::vector<std::string> messages;
    Json::Value j_messageList;
    
    if(payload.length()==0){ //using cached data
        try{
            j_messageList = j_messageMap.at(channel_id);
        }catch(const std::out_of_range &oor){
            pthread_mutex_lock(&output_lock);
            std::cout<< "unkown channel id given: "+ channel_id + "\n";
            pthread_mutex_unlock(&output_lock);
        }
    }else{
        Json::Value jObj = parse_payload(payload);
        j_messageList = jObj.get("messages", "default");
        //pthread_mutex_lock(&map_lock);
        edit_map(channel_id, j_messageList, j_messageMap);
        //j_messageMap.insert(std::pair<std::string, Json::Value>(channel_id, j_messageList));
        //pthread_mutex_unlock(&map_lock);
    }
    int messageIndex = 0;
    while(j_messageList.isValidIndex(messageIndex)){
        if(isMessage(j_messageList[messageIndex])){
            messages.push_back(extract_text(j_messageList[messageIndex]));
        }
        messageIndex++;
    }
    return messages;
}


std::string get_channel_id(Json::Value j_channel){
    std::string channel_id = "";
    const char* id = "id";
    channel_id = j_channel[id].asString(); //j_channel[id].toStyledString();
    return channel_id;
}

std::vector<std::string> add_messageMap(std::string slack_token, Json::Value j_channel, std::map<std::string, Json::Value> & j_messageMap){
    std::string payload = "";
    std::string methodName = "channels.history";
    std::string channel_id = j_channel["id"].asString(); //get_channel_id(j_channel);
    std::string param = "&channel=" + channel_id;
    call_slack(slack_token, methodName, payload, param, j_messageMap);
    std::vector<std::string> messages = extract_messages(payload, channel_id, j_messageMap);
    return messages;
}

void show_message_history(std::string slack_token, Json::Value j_channel, std::map<std::string, Json::Value>& j_messageMap){
    std::vector<std::string> messages = add_messageMap(slack_token, j_channel, j_messageMap);
    //cache_message_history(slack_token, j_channel, j_messageMap);
    pthread_mutex_lock(&output_lock);
    std::cout << j_channel["name"].asString()<<" - Message History: \n";
    for(int message_index = 0; message_index < messages.size(); message_index++){
        std::cout << messages[message_index] << "\n";
    }
    pthread_mutex_unlock(&output_lock);
    cache_upkeep("messages", j_messageMap);
}

const void show_message_history_num(std::string slack_token, std::string channel, std::map<std::string, Json::Value> &j_channelMap, std::map<std::string, Json::Value> &j_messageMap ){
    show_message_history(slack_token, j_channelMap[channel], j_messageMap);
}

struct threadData{
    std::string slack_token;
    std::string channel_name;
    Json::Value channel;
    std::map<std::string, Json::Value> *j_channelMap_ptr;
    std::map<std::string, Json::Value> *j_messageMap_ptr;
    int num;
};

void* thread_show_history_num(void* threadarg){
    struct threadData *mydata;
    mydata = (threadData*)threadarg;
    show_message_history_num(mydata->slack_token, mydata->channel_name, *(mydata->j_channelMap_ptr), *(mydata->j_messageMap_ptr));
    pthread_exit(NULL);
    return NULL;
}

void* thread_show_history(void* threadarg){
    struct threadData *mydata;
    mydata = (threadData*)threadarg;
    show_message_history(mydata->slack_token, mydata->channel, *(mydata->j_messageMap_ptr));
    pthread_exit(NULL);
    return NULL;
}



void select_channel(std::string slack_token, std::vector<std::string> channelList,  std::map<std::string, Json::Value> &j_channelMap, std::map<std::string, Json::Value> &j_messageMap){
    void * status;
    char selected[CHANNEL_NAME_MAX_LENGTH];
    
    std::cout << "select channel (number or name) for message history: ";
    std::cin >> selected;
    pthread_t threads[1];
    struct threadData td;
    td.slack_token = slack_token;
    
    td.j_channelMap_ptr = &j_channelMap;
    td.j_messageMap_ptr = &j_messageMap;
    if(isdigit(selected[0])){
        int num;
        
        std::stringstream convert(selected);
        if(!(convert>> num)){
            num = 1;
        }
        num--;
        td.channel_name = channelList[num];
        int rc = pthread_create(&threads[0], NULL, thread_show_history_num, (void*)&td);
        if(rc){
            std::cout << "couldn't create thread";
            exit(-1);
        }
        
        for(int i = 0; i <channelList.size(); i++){
            if(i != num){
                add_messageMap(slack_token, j_channelMap[channelList[i]], j_messageMap);
            }
        }
        
        //cache_message_history_num(slack_token, channelList[i], j_channelMap, j_messageMap);
        
    }else{
        td.channel = j_channelMap[selected];
        int rc = pthread_create(&threads[0], NULL, thread_show_history, (void*)&td);
        if(rc){
            std::cout << "couldn't create thread";
            exit(-1);
        }
        
        for(std::map<std::string, Json::Value>::iterator it = j_channelMap.begin(); it != j_channelMap.end(); ++it){
            if(std::strcmp(selected, it->first.c_str()) != 0){
                add_messageMap(slack_token, it->second, j_messageMap);
                //add_messageMap(slack_token, j_channelMap[it->first.c_str()], j_messageMap);
                //cache_message_history(slack_token, j_channelMap[it->first.c_str()], j_messageMap);
            }
        }
        
    }
    //pthread_mutex_unlock(&size_lock);
    cache_upkeep("messages", j_messageMap);
    pthread_join(threads[0], &status);
}


void init(std::map<std::string, Json::Value>& j_channelMap, std::map<std::string, Json::Value>& j_messageMap){
    build_cacheMap("channels", j_channelMap);
    build_cacheMap("messages", j_messageMap);
    pthread_mutex_init(&output_lock, NULL);
    pthread_mutex_init(&map_lock, NULL);
    pthread_mutex_init(&file_lock,NULL);
}

int main(int argc, const char * argv[]) {
    // insert code here...
    /*Pseudo code to implement:
     1)intro output
     */
    std::map<std::string, Json::Value> j_channelMap;
    std::map<std::string, Json::Value> j_messageMap;
    std::string slack_token;
    init(j_channelMap, j_messageMap);
    std::cout << "Welcome to Slack!\n";
    std::cout << "Please provide token: ";
    
    std::cin >> slack_token;
     //2)get/list all available channels
    std::vector<std::string> channelList = get_channel_list(slack_token, j_channelMap);
    //int sem_num = -channelList.size() +1;
    //map_lock = sem_open("map_lock", O_CREAT, 0644, sem_num);
     //3)wait for select channel/show message history
    select_channel(slack_token, channelList, j_channelMap, j_messageMap);
//    std::cout << "final shabang!\n";
//    for(std::map<std::string, Json::Value>::iterator it=j_messageMap.begin(); it!=j_messageMap.end(); ++it){
//        std::cout << it->first << " : " << it->second << "\n";
//    }
//    
     //4)cache data
    pthread_mutex_destroy(&map_lock);
    pthread_mutex_destroy(&file_lock);
    pthread_mutex_destroy(&output_lock);
    //pthread_exit(NULL);

    return 0;
}
